$ErrorActionPreference='Stop'
$nodivuRoot=Split-Path $PSScriptRoot -Parent
$oldId=$env:NODIVU_RECOVERY_TEST_ID
$oldStage=$env:NODIVU_RECOVERY_TEST_STAGE
$env:NODIVU_RECOVERY_TEST_ID=[guid]::NewGuid().ToString()
try {
    foreach($stage in @('write','restore')) {
        $env:NODIVU_RECOVERY_TEST_STAGE=$stage
        $stdout=Join-Path $nodivuRoot ".local/recovery-$stage.stdout.log"
        $stderr=Join-Path $nodivuRoot ".local/recovery-$stage.stderr.log"
        $p=Start-Process (Join-Path $nodivuRoot '.local/electron/nodivu-electron.exe') -ArgumentList @('--smoke-test','--test-recovery') -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null=$p.Handle
        if(-not $p.WaitForExit(45000)){$p.Kill();throw "Recuperação $stage excedeu 45 segundos"}
        $p.WaitForExit();$code=$p.ExitCode;$p.Dispose()
        Get-Content -LiteralPath $stdout -Encoding UTF8
        Get-Content -LiteralPath $stderr -Encoding UTF8
        $expected=if($stage -eq 'write'){71}else{0}
        if($code -ne $expected){throw "Recuperação $stage falhou: $code (esperado $expected)"}
    }
    $copy=Join-Path ([IO.Path]::GetTempPath()) "Nodivu-recovery-smoke-$env:NODIVU_RECOVERY_TEST_ID/recovery/recovery.nodivu.json"
    if(Test-Path -LiteralPath $copy){throw 'Fechar com descarte confirmado deixou copia obsoleta'}
    Write-Output 'PASS: descarte confirmado no fechamento remove somente a copia de recuperacao.'
    Write-Output "Evidência: $env:TEMP/Nodivu-recovery-smoke-$env:NODIVU_RECOVERY_TEST_ID"
} finally {$env:NODIVU_RECOVERY_TEST_ID=$oldId;$env:NODIVU_RECOVERY_TEST_STAGE=$oldStage}
