//! Formatos PCM na fronteira WASAPI; nenhuma alocação no decoder.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(super) struct Format {
    pub rate: u32,
    pub channels: u16,
    pub bits: u16,
    pub valid_bits: u16,
    pub tag: u16,
    pub align: u16,
    pub bytes_per_second: u32,
}
impl Format {
    pub fn supported(self) -> bool {
        // Limites de trabalho/memória, não uma lista de taxas de microfone.
        (8_000..=384_000).contains(&self.rate)
            && (1..=32).contains(&self.channels)
            && ((self.tag == 1 && [8, 16, 24, 32].contains(&self.bits))
                || (self.tag == 3 && self.bits == 32))
            && self.valid_bits > 0
            && self.valid_bits <= self.bits
            && (self.tag != 3 || self.valid_bits == 32)
            && u32::from(self.align) == u32::from(self.channels) * u32::from(self.bits / 8)
            && self.rate.checked_mul(u32::from(self.align)) == Some(self.bytes_per_second)
    }
    pub fn native_capture(self) -> bool {
        self.supported() && self.rate <= 48_000 && self.channels <= 2
    }
    pub fn sample_bytes(self) -> usize {
        usize::from(self.bits / 8)
    }
    /// Bytes little-endian completos, validados pelo chamador. PCM extensível usa
    /// bits válidos alinhados à esquerda: normalizar pelo container preserva 0 dB.
    pub fn sample(self, bytes: &[u8]) -> f32 {
        match (self.tag, self.bits) {
            (3, 32) => f32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
            (1, 8) => (f32::from(bytes[0]) - 128.0) / 128.0,
            (1, 16) => f32::from(i16::from_le_bytes([bytes[0], bytes[1]])) / 32768.0,
            (1, 24) => {
                (i32::from_le_bytes([0, bytes[0], bytes[1], bytes[2]]) as f32) / 2147483648.0
            }
            (1, 32) => {
                (i32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]) as f32) / 2147483648.0
            }
            _ => 0.0, // Inalcançável após supported(), sem panic no caminho de áudio.
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Format;
    fn pcm(rate: u32, channels: u16, bits: u16) -> Format {
        Format {
            rate,
            channels,
            bits,
            valid_bits: bits,
            tag: 1,
            align: channels * (bits / 8),
            bytes_per_second: rate * u32::from(channels * (bits / 8)),
        }
    }
    #[test]
    fn common_microphones_and_interfaces_have_a_conversion_path() {
        for rate in [
            8_000, 11_025, 16_000, 22_050, 24_000, 32_000, 44_100, 48_000, 88_200, 96_000, 192_000,
            384_000,
        ] {
            for channels in [1, 2, 4, 8, 32] {
                for bits in [8, 16, 24, 32] {
                    let format = pcm(rate, channels, bits);
                    assert!(format.supported());
                    assert_eq!(format.native_capture(), rate <= 48_000 && channels <= 2);
                }
            }
        }
        let mut format = pcm(16_000, 1, 16);
        format.align = 4;
        assert!(!format.supported());
        assert!(!pcm(0, 1, 16).supported());
        assert!(!pcm(48_000, 33, 16).supported());
    }
    #[test]
    fn pcm_containers_preserve_sign_and_amplitude_including_24_in_32() {
        for (bits, negative, positive) in [
            (8, vec![0], vec![192]),
            (16, vec![0, 128], vec![0, 64]),
            (24, vec![0, 0, 128], vec![0, 0, 64]),
            (32, vec![0, 0, 0, 128], vec![0, 0, 0, 64]),
        ] {
            let mut format = pcm(16_000, 1, bits);
            assert_eq!(format.sample(&negative), -1.0);
            assert_eq!(format.sample(&positive), 0.5);
            if bits == 32 {
                format.valid_bits = 24;
                assert!(format.supported());
                assert_eq!(format.sample(&positive), 0.5);
            }
        }
        let mut float = pcm(48_000, 2, 32);
        float.tag = 3;
        assert!(float.supported());
        assert_eq!(float.sample(&(-0.75_f32).to_le_bytes()), -0.75);
    }
}
