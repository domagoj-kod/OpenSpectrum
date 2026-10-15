# Frame timing on native Windows from a capture played back: one run per FFT
# size, then a table of medians of the FRAME-TIMING windows (the first five
# skipped while the app settles). Run from the folder that holds
# openspectrum.exe:
#   powershell -ExecutionPolicy Bypass -File play-bench.ps1 -cap data\x.iq
# An A/B runs the other build's exe from here, so both logs land together,
# and alternates the two, since the machine drifts over a session:
#   .\play-bench.ps1 -cap data\x.iq -exe ..\other\openspectrum.exe -tag a1
param(
  [Parameter(Mandatory)][string]$cap,
  [string]$exe = '.\openspectrum.exe',
  [int[]]$sizes = (4096, 65536),
  [int]$secs = 60,
  [string]$tag = 'play'
)

function Median($xs) {
  $s = @($xs | Sort-Object)
  $n = $s.Count
  if ($n % 2) { $s[($n - 1) / 2] } else { ($s[$n / 2 - 1] + $s[$n / 2]) / 2 }
}

$rows = foreach ($fft in $sizes) {
  $log = "${tag}_$fft"
  $p = Start-Process $exe -PassThru `
    -RedirectStandardOutput "$log.out.log" -RedirectStandardError "$log.err.log" `
    -ArgumentList "--play `"$cap`" -s $fft"
  Start-Sleep $secs
  # A hard stop: the tail of stdout still in the app's buffer is lost, which
  # only shortens the table by a few windows.
  if (-not $p.HasExited) { Stop-Process $p }
  $p.WaitForExit()

  $w = @(Select-String 'FRAME-TIMING fps=([\d.]+).*cpu avg=([\d.]+).*render_build avg=([\d.]+).*present avg=([\d.]+)' "$log.out.log" |
      Select-Object -Skip 5 | ForEach-Object { , $_.Matches[0].Groups })
  if ($w.Count -eq 0) {
    Write-Warning "${log}: no FRAME-TIMING lines, see $log.*.log"
    continue
  }
  [pscustomobject]@{
    tag          = $tag
    fft          = $fft
    windows      = $w.Count
    fps          = Median ($w | ForEach-Object { [double]$_[1].Value })
    fps_min      = ($w | ForEach-Object { [double]$_[1].Value } | Measure-Object -Minimum).Minimum
    cpu          = Median ($w | ForEach-Object { [double]$_[2].Value })
    render_build = Median ($w | ForEach-Object { [double]$_[3].Value })
    present      = Median ($w | ForEach-Object { [double]$_[4].Value })
  }
}
$rows | Format-Table -AutoSize
$rows | Export-Csv play-bench.csv -Append -NoTypeInformation
