//! Fronteira WASAPI. Todos os objetos, eventos e buffers pertencem a esta thread.
use crate::{
    audio::{AudioConfig, Shared},
    dsp::{BLOCK_FRAMES, Bus, RATE},
    windows_backend::Apartment,
};
use std::{
    sync::atomic::Ordering,
    time::{Duration, Instant},
};
use windows::{
    Win32::{
        Foundation::*,
        Media::Audio::*,
        System::{
            Com::*,
            Threading::{
                AvRevertMmThreadCharacteristics, AvSetMmThreadCharacteristicsW, CreateEventW,
                WaitForMultipleObjects,
            },
        },
    },
    core::{GUID, Interface, PCWSTR, Result, w},
};

#[path = "windows_audio_format.rs"]
mod format;
use format::Format;

struct Event(HANDLE);
struct AudioScheduling(HANDLE);
impl AudioScheduling {
    fn new() -> Result<Self> {
        let mut index = 0;
        // SAFETY: registra somente a thread atual no perfil MMCSS Audio; não muda
        // prioridade do processo ou configurações globais. Guard reverte na mesma thread.
        unsafe {
            Ok(Self(AvSetMmThreadCharacteristicsW(
                w!("Audio"),
                &mut index,
            )?))
        }
    }
}
impl Drop for AudioScheduling {
    fn drop(&mut self) {
        // SAFETY: handle de registro válido, reversão única na thread proprietária.
        let _ = unsafe { AvRevertMmThreadCharacteristics(self.0) };
    }
}
impl Event {
    fn new() -> Result<Self> {
        // SAFETY: evento privado auto-reset, sem nome nem security descriptor externo.
        unsafe { Ok(Self(CreateEventW(None, false, false, PCWSTR::null())?)) }
    }
}
impl Drop for Event {
    fn drop(&mut self) {
        // SAFETY: único owner, nenhuma espera pendente na thread durante teardown.
        let _ = unsafe { CloseHandle(self.0) };
    }
}
struct MixFormat(*mut WAVEFORMATEX);
impl Drop for MixFormat {
    fn drop(&mut self) {
        // SAFETY: GetMixFormat usa CoTaskMemAlloc; esta alocação nunca escapa.
        unsafe { CoTaskMemFree(Some(self.0.cast())) }
    }
}
struct Stream {
    client: IAudioClient,
    event: Event,
    channels: usize,
    frames: u32,
    mix_rate: u32,
    period_100ns: u64,
    format: Format,
}

fn query_periods(
    client: &IAudioClient,
    mix: &MixFormat,
    rate: u32,
    target: &crate::diagnostics::DevicePeriods,
) {
    let result = (|| -> Result<()> {
        let client3: IAudioClient3 = client.cast()?;
        let (mut default, mut fundamental, mut min, mut max) = (0, 0, 0, 0);
        // SAFETY: mix veio de GetMixFormat, foi validado em open e segue vivo nesta chamada.
        // Consulta no controle de abertura, fora de buffers/áudio; não muda periodicidade.
        unsafe {
            client3.GetSharedModeEnginePeriod(
                mix.0,
                &mut default,
                &mut fundamental,
                &mut min,
                &mut max,
            )?;
        }
        if rate == 0 || fundamental == 0 || min == 0 || min > default || default > max {
            return Err(E_UNEXPECTED.into());
        }
        target.rate.store(rate, Ordering::Relaxed);
        target.default_frames.store(default, Ordering::Relaxed);
        target
            .fundamental_frames
            .store(fundamental, Ordering::Relaxed);
        target.min_frames.store(min, Ordering::Relaxed);
        target.max_frames.store(max, Ordering::Relaxed);
        Ok(())
    })();
    match result {
        Ok(()) => target.status.store(1, Ordering::Release),
        Err(error) => {
            target.error.store(error.code().0, Ordering::Relaxed);
            target.status.store(2, Ordering::Release);
        }
    }
}
impl Drop for Stream {
    fn drop(&mut self) {
        // SAFETY: Stop e Release na thread/apartamento de criação. Stop é best effort
        // em dispositivo removido; Release do client ainda libera a referência local.
        let _ = unsafe { self.client.Stop() };
    }
}

fn open(
    enumerator: &IMMDeviceEnumerator,
    id: &str,
    shared: &Shared,
    capture: bool,
    capture_minimum: bool,
) -> Result<Stream> {
    let wide: Vec<u16> = id.encode_utf16().chain(Some(0)).collect();
    shared.operation.store(1, Ordering::Relaxed);
    // SAFETY: UTF-16 terminado e vivo na chamada; o ID é o explicitamente selecionado.
    let device = unsafe { enumerator.GetDevice(PCWSTR(wide.as_ptr()))? };
    shared.operation.store(2, Ordering::Relaxed);
    // SAFETY: COM inicializado nesta thread; interfaces locais com ownership RAII.
    let client: IAudioClient = unsafe { device.Activate(CLSCTX_ALL, None)? };
    shared.operation.store(3, Ordering::Relaxed);
    // SAFETY: resultado possui memória COM e é protegido mesmo em falha de validação.
    let mix = MixFormat(unsafe { client.GetMixFormat()? });
    if mix.0.is_null() {
        return Err(E_POINTER.into());
    }
    // SAFETY: API garante ao menos WAVEFORMATEX; cópia evita referências a campos packed.
    let format = unsafe { mix.0.read_unaligned() };
    let mut tag = format.wFormatTag;
    let mut valid_bits = format.wBitsPerSample;
    if tag == 0xfffe && format.cbSize >= 22 {
        // SAFETY: cbSize comprovou a extensão WAVEFORMATEXTENSIBLE.
        let extension = unsafe { mix.0.cast::<WAVEFORMATEXTENSIBLE>().read_unaligned() };
        let sub_format = extension.SubFormat;
        // SAFETY: extensão validada; union contém wValidBitsPerSample para PCM/float.
        valid_bits = unsafe { extension.Samples.wValidBitsPerSample };
        tag = if sub_format == GUID::from_u128(0x00000003_0000_0010_8000_00aa00389b71) {
            3
        } else if sub_format == GUID::from_u128(0x00000001_0000_0010_8000_00aa00389b71) {
            1
        } else {
            0
        };
    }
    let mix_rate = format.nSamplesPerSec;
    let mix_channels = format.nChannels;
    let observed = Format {
        rate: mix_rate,
        channels: mix_channels,
        bits: format.wBitsPerSample,
        valid_bits,
        tag,
        align: format.nBlockAlign,
        bytes_per_second: format.nAvgBytesPerSec,
    };
    let (rate, channels, bits, encoding) = if capture {
        (
            &shared.capture_rate,
            &shared.capture_channels,
            &shared.capture_bits,
            &shared.capture_tag,
        )
    } else {
        (
            &shared.output_rate,
            &shared.output_channels,
            &shared.output_bits,
            &shared.output_tag,
        )
    };
    // Publicar antes de validar: falhas também precisam mostrar o formato observado.
    rate.store(mix_rate, Ordering::Relaxed);
    channels.store(u32::from(mix_channels), Ordering::Relaxed);
    bits.store(u32::from(observed.bits), Ordering::Relaxed);
    encoding.store(u32::from(tag), Ordering::Relaxed);
    if !observed.supported() {
        return Err(AUDCLNT_E_UNSUPPORTED_FORMAT.into());
    }
    let native_capture = capture && observed.native_capture();
    if capture && capture_minimum && !native_capture {
        // Não prometer período mínimo quando a conversão exige Initialize clássico.
        return Err(AUDCLNT_E_UNSUPPORTED_FORMAT.into());
    }
    // Taxas altas precisam de filtro antialias; delegar conversão/matriz ao Windows
    // evita decimação ingênua e interpretações incorretas de arrays multicanal.
    let channels = mix_channels.min(2);
    let requested = WAVEFORMATEXTENSIBLE {
        Format: WAVEFORMATEX {
            wFormatTag: 0xfffe,
            nChannels: channels,
            nSamplesPerSec: RATE,
            nAvgBytesPerSec: RATE * u32::from(channels) * 4,
            nBlockAlign: channels * 4,
            wBitsPerSample: 32,
            cbSize: 22,
        },
        Samples: WAVEFORMATEXTENSIBLE_0 {
            wValidBitsPerSample: 32,
        },
        dwChannelMask: if channels == 1 { 4 } else { 3 },
        SubFormat: GUID::from_u128(0x00000003_0000_0010_8000_00aa00389b71),
    };
    let event = Event::new()?;
    query_periods(
        &client,
        &mix,
        mix_rate,
        if capture {
            &shared.capture_periods
        } else {
            &shared.output_periods
        },
    );
    let mut period = 0_i64;
    shared.operation.store(4, Ordering::Relaxed);
    // SAFETY: formato válido durante Initialize; shared/event mode exige duração e
    // periodicidade zero. Evento permanece vivo até a liberação do client.
    unsafe {
        client.GetDevicePeriod(Some(&mut period), None)?;
        shared.operation.store(5, Ordering::Relaxed);
        if native_capture && capture_minimum {
            let periods = &shared.capture_periods;
            periods.minimum_requested.store(1, Ordering::Relaxed);
            if periods.status.load(Ordering::Acquire) != 1 {
                return Err(windows::core::Error::from(windows::core::HRESULT(
                    periods.error.load(Ordering::Relaxed),
                )));
            }
            let frames = periods.min_frames.load(Ordering::Relaxed);
            let fundamental = periods.fundamental_frames.load(Ordering::Relaxed);
            if fundamental == 0 || !frames.is_multiple_of(fundamental) {
                return Err(AUDCLNT_E_INVALID_DEVICE_PERIOD.into());
            }
            // Captura no mix format validado; sem AUTOCONVERTPCM, não suportado nesta API.
            // Falha experimental é explícita, sem fallback para outra configuração/dispositivo.
            let client3: IAudioClient3 = client.cast()?;
            client3.InitializeSharedAudioStream(
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                frames,
                mix.0,
                None,
            )?;
            period = i64::from(frames) * 10_000_000 / i64::from(mix_rate);
        } else {
            client.Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                    | if native_capture {
                        0
                    } else {
                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
                    },
                0,
                0,
                if native_capture {
                    mix.0
                } else {
                    &requested.Format
                },
                None,
            )?;
        }
        shared.operation.store(6, Ordering::Relaxed);
        client.SetEventHandle(event.0)?;
    }
    shared.operation.store(7, Ordering::Relaxed);
    // SAFETY: client inicializado; tamanho real é consultado, não presumido.
    let frames = unsafe { client.GetBufferSize()? };
    let stream_rate = if native_capture { mix_rate } else { RATE };
    if frames == 0 || frames > stream_rate / 5 {
        return Err(AUDCLNT_E_BUFFER_SIZE_ERROR.into());
    }
    Ok(Stream {
        client,
        event,
        channels: usize::from(channels),
        frames,
        mix_rate: stream_rate,
        period_100ns: period.max(0) as u64,
        format: if native_capture {
            observed
        } else {
            Format {
                rate: RATE,
                channels,
                bits: 32,
                valid_bits: 32,
                tag: 3,
                align: channels * 4,
                bytes_per_second: RATE * u32::from(channels) * 4,
            }
        },
    })
}

fn capture_packet(
    capture: &IAudioCaptureClient,
    stream: &Stream,
    bus: &mut Bus,
    shared: &Shared,
) -> Result<bool> {
    let started = Instant::now();
    // SAFETY: service e stream válidos na mesma thread, sem buffer pendente.
    if unsafe { capture.GetNextPacketSize()? } == 0 {
        return Ok(false);
    }
    let mut pointer = std::ptr::null_mut();
    let mut frames = 0;
    let mut flags = 0;
    // SAFETY: out-pointers válidos; ReleaseBuffer abaixo ocorre antes de retornar,
    // inclusive se o tamanho/ponteiro retornado for inesperado.
    unsafe {
        capture.GetBuffer(&mut pointer, &mut frames, &mut flags, None, None)?;
    }
    let silent = flags & AUDCLNT_BUFFERFLAGS_SILENT.0 as u32 != 0;
    let valid = frames <= stream.frames && (silent || !pointer.is_null());
    if valid {
        if flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY.0 as u32 != 0 {
            shared.discontinuities.fetch_add(1, Ordering::Relaxed);
            bus.clear_capture();
        }
        for frame in 0..frames as usize {
            let samples = if silent {
                [0.0; 2]
            } else {
                let width = stream.format.sample_bytes();
                let size = stream.channels * width;
                // SAFETY: frames <= buffer do cliente; formato/layout validados em open.
                // Slice cobre exatamente um frame e não vive após ReleaseBuffer.
                let bytes = unsafe { std::slice::from_raw_parts(pointer.add(frame * size), size) };
                let left = stream.format.sample(&bytes[..width]);
                [
                    left,
                    if stream.channels == 2 {
                        stream.format.sample(&bytes[width..])
                    } else {
                        left
                    },
                ]
            };
            bus.push(samples);
        }
        shared
            .captured_frames
            .fetch_add(u64::from(frames), Ordering::Relaxed);
    }
    // SAFETY: encerra exatamente o pacote adquirido acima na mesma thread.
    unsafe {
        capture.ReleaseBuffer(frames)?;
    }
    if !valid {
        return Err(E_UNEXPECTED.into());
    }
    bus.timings[0].finish(started, frames as usize, stream.mix_rate);
    Ok(true)
}

fn render_packet(
    render: &IAudioRenderClient,
    stream: &Stream,
    bus: &mut Bus,
    shared: &Shared,
    block: &mut [[f32; 2]; BLOCK_FRAMES],
    effect: &mut Option<&mut crate::external_audio::StereoEffect<'_>>,
) -> Result<u32> {
    let started = Instant::now();
    // SAFETY: client pertence ao worker; padding limita a escrita ao espaço disponível.
    let padding = unsafe { stream.client.GetCurrentPadding()? };
    let frames = stream
        .frames
        .checked_sub(padding)
        .ok_or_else(|| windows::core::Error::from(E_UNEXPECTED))?;
    if padding == 0 && shared.state.load(Ordering::Acquire) == 1 {
        shared.render_starvations.fetch_add(1, Ordering::Relaxed);
    }
    if frames == 0 {
        return Ok(0);
    }
    // SAFETY: aquisição limitada à capacidade livre obtida acima.
    let pointer = unsafe { render.GetBuffer(frames)? };
    if pointer.is_null() {
        // SAFETY: liberar aquisição com silêncio mesmo se o driver retornar ponteiro inválido.
        unsafe {
            render.ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT.0 as u32)?;
        }
        return Err(E_POINTER.into());
    }
    let mut offset = 0;
    while offset < frames as usize {
        let count = BLOCK_FRAMES.min(frames as usize - offset);
        bus.render_with_effect(&mut block[..count], effect);

        for (i, frame) in block[..count].iter().enumerate() {
            // SAFETY: faixa da aquisição; formato float32/canais 1 ou 2 negociado em open.
            unsafe {
                let p = pointer.cast::<f32>().add((offset + i) * stream.channels);
                if stream.channels == 1 {
                    p.write_unaligned((frame[0] + frame[1]) * 0.5);
                } else {
                    p.write_unaligned(frame[0]);
                    p.add(1).write_unaligned(frame[1]);
                }
            }
        }
        // Final mix after nodes/mute and the primary channel conversion.
        if stream.channels == 1 {
            for f in &mut block[..count] {
                let mono = (f[0] + f[1]) * 0.5;
                *f = [mono, mono];
            }
        }
        shared.monitor.push(&block[..count]);
        offset += count;
    }
    // SAFETY: todos os frames foram escritos e não serão acessados após ReleaseBuffer.
    unsafe {
        render.ReleaseBuffer(frames, 0)?;
    }
    shared
        .rendered_frames
        .fetch_add(u64::from(frames), Ordering::Relaxed);
    bus.timings[5].finish(started, frames as usize, RATE);
    Ok(frames)
}

fn publish(bus: &mut Bus, shared: &Shared) {
    let plan = bus.pipeline.plan.live.unwrap_or_default();
    shared
        .route_latency
        .store(plan.latency_frames, Ordering::Relaxed);
    shared
        .route_delay
        .store(plan.delay_frames, Ordering::Relaxed);
    for (i, target) in shared.route_nodes.iter().enumerate() {
        target.publish(plan.steps[i].id, bus.routing.peaks[i], bus.routing.calls[i]);
        bus.routing.peaks[i] = 0.0;
    }
    shared.external_timing.publish(&bus.external_timing);
    shared
        .external_input_peak
        .store(bus.external_input_peak.to_bits(), Ordering::Relaxed);
    shared
        .external_output_peak
        .store(bus.external_output_peak.to_bits(), Ordering::Relaxed);
    bus.external_input_peak = 0.0;
    bus.external_output_peak = 0.0;
    for (i, (target, slot)) in shared.nodes.iter().zip(&mut bus.pipeline.slots).enumerate() {
        let plan = &bus.pipeline.plan;
        target.publish(
            slot,
            plan.live.map_or_else(
                || plan.order[..usize::from(plan.count)].contains(&(i as u8)),
                |p| {
                    p.steps[..usize::from(p.count)]
                        .iter()
                        .any(|s| s.kind == 3 && usize::from(s.slot) == i)
                },
            ),
            plan.gains[i].is_some_and(|g| g.bypass),
        );
        slot.peak = 0.0;
    }
    for (target, timing) in shared.timings.iter().zip(&bus.timings) {
        target.publish(timing);
    }
    shared
        .processor_errors
        .store(bus.counters.processor_errors, Ordering::Relaxed);
    shared
        .input_peak
        .store(bus.input_peak.to_bits(), Ordering::Relaxed);
    shared
        .output_peak
        .store(bus.output_peak.to_bits(), Ordering::Relaxed);
    shared
        .underruns
        .store(bus.counters.underruns, Ordering::Relaxed);
    shared
        .overflows
        .store(bus.counters.overflows, Ordering::Relaxed);
    shared
        .invalid_samples
        .store(bus.counters.invalid_samples, Ordering::Relaxed);
    shared
        .clipped_samples
        .store(bus.counters.clipped_samples, Ordering::Relaxed);
    shared
        .occupancy
        .store(bus.occupancy() as u32, Ordering::Relaxed);
    bus.input_peak = 0.0;
    bus.output_peak = 0.0;
}

pub(crate) fn run(
    config: AudioConfig,
    shared: &Shared,
    target_ms: u32,
    capture_minimum: bool,
) -> Result<()> {
    run_with_effect(config, shared, target_ms, capture_minimum, None)
}

pub(crate) fn run_with_effect(
    config: AudioConfig,
    shared: &Shared,
    target_ms: u32,
    capture_minimum: bool,
    mut effect: Option<&mut crate::external_audio::StereoEffect<'_>>,
) -> Result<()> {
    let _apartment = Apartment::new()?;
    // SAFETY: COM inicializado; enumerator será destruído antes do Apartment.
    let enumerator: IMMDeviceEnumerator =
        unsafe { CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)? };
    shared.stage.store(1, Ordering::Relaxed);
    // A entrada é independente: falha de abertura/serviço/Start silencia somente
    // o microfone. Player, tom e capturas de aplicativos continuam na mesma saída.
    let mut input = None;
    let mut capture: Option<IAudioCaptureClient> = None;
    if let Some(id) = &config.capture {
        let opened = (|| -> Result<_> {
            let stream = open(&enumerator, &id.0, shared, true, capture_minimum)?;
            shared.operation.store(8, Ordering::Relaxed);
            // SAFETY: cliente inicializado nesta thread. Serviço cai antes do cliente.
            let service: IAudioCaptureClient = unsafe { stream.client.GetService()? };
            Ok((stream, service))
        })();
        match opened {
            Ok((stream, service)) => {
                input = Some(stream);
                capture = Some(service);
            }
            Err(error) => {
                record_capture_failure(shared, error, shared.operation.load(Ordering::Relaxed))
            }
        }
    }
    shared.stage.store(2, Ordering::Relaxed);
    let output = open(&enumerator, &config.output.0, shared, false, false)?;
    shared.operation.store(8, Ordering::Relaxed);
    // SAFETY: output é client de reprodução inicializado.
    let render: IAudioRenderClient = unsafe { output.client.GetService()? };
    if let Some(input) = &input {
        shared
            .capture_stream_rate
            .store(input.mix_rate, Ordering::Relaxed);
        shared
            .capture_buffer_frames
            .store(input.frames, Ordering::Relaxed);
        shared
            .capture_period_100ns
            .store(input.period_100ns, Ordering::Relaxed);
    }

    shared
        .output_buffer_frames
        .store(output.frames, Ordering::Relaxed);
    shared
        .output_period_100ns
        .store(output.period_100ns, Ordering::Relaxed);
    let capture_rate = input.as_ref().map_or(RATE, |s| s.mix_rate);
    let mut bus = if target_ms == 20 {
        Bus::new(capture_rate)
    } else {
        Bus::with_target_ms(capture_rate, target_ms)
    };
    shared.capture_target_frames.store(
        if input.is_some() {
            bus.target_frames() as u32
        } else {
            0
        },
        Ordering::Relaxed,
    );
    let mut block = [[0.0; 2]; BLOCK_FRAMES];
    // Preencher silêncio antes de Start evita reproduzir memória não inicializada.
    render_packet(&render, &output, &mut bus, shared, &mut block, &mut effect)?;
    if shared.stop.load(Ordering::Relaxed) {
        return Ok(());
    }
    let _scheduling = AudioScheduling::new()?;
    // Buffers já preparados. Erro de Start também fica restrito ao mic.
    if let Some(stream) = &input {
        // SAFETY: stream inicializado nesta thread; captura explicitamente selecionada.
        if let Err(error) = unsafe { stream.client.Start() } {
            record_capture_failure(shared, error, 9);
            capture = None; // liberar serviço antes do client
            input = None;
            bus.clear_capture();
            shared.capture_target_frames.store(0, Ordering::Relaxed);
        }
    }
    shared.stage.store(2, Ordering::Relaxed);
    shared.operation.store(9, Ordering::Relaxed);
    // SAFETY: render contém silêncio preparado; falha fecha também captura por RAII.
    unsafe {
        output.client.Start()?;
    }
    shared.operation.store(10, Ordering::Relaxed);
    shared.state.store(1, Ordering::Release);
    let mut events = vec![output.event.0];
    if let Some(input) = &input {
        events.push(input.event.0);
    }
    let mut capture_alive = input.is_some();
    let mut window_frames = 0_u32;
    let mut last_render = Instant::now();
    let mut stopping = None;
    loop {
        if shared.stop.load(Ordering::Relaxed) && stopping.is_none() {
            stopping = Some(Instant::now());
        }
        if stopping.is_some_and(|since: Instant| since.elapsed() >= Duration::from_millis(60)) {
            break;
        }
        // SAFETY: handles privados vivos; espera acontece fora de qualquer buffer adquirido.
        let wait = unsafe { WaitForMultipleObjects(&events, false, 20) };
        if wait == WAIT_FAILED {
            return Err(windows::core::Error::from_thread());
        }
        if let Some(plan) = shared.plan.read() {
            bus.request_plan(plan);
        }
        if stopping.is_some() {
            bus.begin_stop();
        }
        if capture_alive && let (Some(capture), Some(input)) = (&capture, &input) {
            // No máximo 32 pacotes por ativação; nenhum produtor pode monopolizar render.
            for _ in 0..32 {
                match capture_packet(capture, input, &mut bus, shared) {
                    Ok(true) => {}
                    Ok(false) => break,
                    Err(error) => {
                        record_capture_failure(shared, error, 10);
                        bus.clear_capture();
                        capture_alive = false;
                        // SAFETY: parar somente a fonte com erro, sem alterar saída/padrão.
                        let _ = unsafe { input.client.Stop() };
                        break;
                    }
                }
            }
        }
        let frames = render_packet(&render, &output, &mut bus, shared, &mut block, &mut effect)?;
        if frames > 0 {
            last_render = Instant::now();
        }
        if last_render.elapsed() > Duration::from_secs(2) {
            return Err(AUDCLNT_E_DEVICE_INVALIDATED.into());
        }
        window_frames += frames;
        if window_frames >= RATE / 10 {
            publish(&mut bus, shared);
            window_frames %= RATE / 10;
        }
    }
    publish(&mut bus, shared);
    Ok(())
}

fn record_capture_failure(shared: &Shared, error: windows::core::Error, operation: u32) {
    shared
        .capture_failure_operation
        .store(operation, Ordering::Relaxed);
    shared
        .capture_failure
        .store(error.code().0, Ordering::Release);
}

/// Independent sink: errors and pacing never stop the primary render worker.
pub(crate) fn run_monitor(
    endpoint: &nodivu_core::DeviceId,
    primary: &Shared,
    shared: &Shared,
) -> Result<()> {
    let _apartment = Apartment::new()?;
    // SAFETY: COM and every client/service remain on this thread.
    let enumerator: IMMDeviceEnumerator =
        unsafe { CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)? };
    let output = open(&enumerator, &endpoint.0, shared, false, false)?;
    // SAFETY: initialized render client, released before the stream.
    let render: IAudioRenderClient = unsafe { output.client.GetService()? };
    let queue = &primary.monitor;
    queue.trim(0);
    queue.dropped.store(0, Ordering::Relaxed);
    queue.underruns.store(0, Ordering::Relaxed);
    queue.enabled.store(true, Ordering::Release);
    let _scheduling = AudioScheduling::new()?;
    // SAFETY: initialized shared-mode client owned by this worker.
    unsafe {
        output.client.Start()?;
    }
    shared.state.store(1, Ordering::Release);
    let mut primed = false;
    let mut last_render = Instant::now();
    while !shared.stop.load(Ordering::Acquire) && !primary.stop.load(Ordering::Acquire) {
        // SAFETY: one live event, bounded wait outside acquired audio buffers.
        if unsafe { WaitForMultipleObjects(&[output.event.0], false, 20) } == WAIT_FAILED {
            return Err(windows::core::Error::from_thread());
        }
        // SAFETY: client owned by this worker; padding bounds buffer acquisition.
        let padding = unsafe { output.client.GetCurrentPadding()? };
        let frames = output
            .frames
            .checked_sub(padding)
            .ok_or_else(|| windows::core::Error::from(E_UNEXPECTED))?;
        if frames == 0 {
            if last_render.elapsed() > Duration::from_secs(2) {
                return Err(AUDCLNT_E_DEVICE_INVALIDATED.into());
            }
            continue;
        }
        last_render = Instant::now();
        // Two clocks may drift: discard oldest backlog, never grow latency without bound.
        if queue.available() > crate::monitor::TARGET * 2 {
            queue.trim(crate::monitor::TARGET);
        }
        if !primed && queue.available() >= crate::monitor::TARGET {
            primed = true;
        }
        // SAFETY: frames <= free capacity; pointer released exactly once below.
        let pointer = unsafe { render.GetBuffer(frames)? };
        if pointer.is_null() {
            // SAFETY: release this acquisition as silent, even for a broken driver pointer.
            unsafe {
                render.ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT.0 as u32)?;
            }
            return Err(E_POINTER.into());
        }
        let mut missing = false;
        for i in 0..frames as usize {
            let frame = if primed {
                queue.pop().unwrap_or_else(|| {
                    missing = true;
                    [0.; 2]
                })
            } else {
                [0.; 2]
            };
            // SAFETY: negotiated float32 mono/stereo, index inside acquired frames.
            unsafe {
                let p = pointer.cast::<f32>().add(i * output.channels);
                if output.channels == 1 {
                    p.write_unaligned((frame[0] + frame[1]) * 0.5);
                } else {
                    p.write_unaligned(frame[0]);
                    p.add(1).write_unaligned(frame[1]);
                }
            }
        }
        if missing {
            primed = false;
            queue.underruns.fetch_add(1, Ordering::Relaxed);
        }
        // SAFETY: every frame initialized, no access after release.
        unsafe {
            render.ReleaseBuffer(frames, 0)?;
        }
    }
    // SAFETY: stop only our own secondary client, never the primary device.
    unsafe {
        output.client.Stop()?;
    }
    Ok(())
}
