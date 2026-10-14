# Sample-rate sweep for a USRP on native Windows: one 20 s cs16 capture per
# rate and FFT size, then a table built from the sidecars. Run from the folder
# that holds openspectrum.exe (a build made with UHD=1):
#   powershell -ExecutionPolicy Bypass -File uhd-sweep.ps1
# Needs about 5 GB free; each capture is deleted once it has been checked.
# To repeat a few rates, from a PowerShell prompt:
#   .\uhd-sweep.ps1 -rates 8e6,16e6 -runs 5
param([double[]]$rates = (2e6, 4e6, 8e6, 16e6, 32e6, 56e6), [int]$runs = 1)
$secs = 20

# Leftovers of an earlier run would match the same names.
Remove-Item data\sweep_*, sweep_*.log -ErrorAction SilentlyContinue

$rows = foreach ($run in 1..$runs) {
  foreach ($fft in 1024, 65536) {
    foreach ($rate in $rates) {
      $tag = "sweep_$($rate / 1e6)M_${fft}_$run"
      $p = Start-Process .\openspectrum.exe -PassThru `
        -RedirectStandardOutput "$tag.out.log" -RedirectStandardError "$tag.err.log" `
        -ArgumentList "--device uhd -f 2440000000 -g 40 -r $rate -s $fft --iq-log --iq-duration $secs --iq-format cs16 --iq-output $tag"
      # The sidecar exists from the start of a capture, with zero counts, and is
      # rewritten when the capture stops; the app itself keeps running. Wait for
      # the rewrite. 60 s covers the FPGA load on the first run.
      $m = $null
      $deadline = (Get-Date).AddSeconds($secs + 60)
      while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        Start-Sleep 1
        $meta = Get-ChildItem "data\${tag}_*.meta.json" -ErrorAction SilentlyContinue
        if (-not $meta) { continue }
        try { $m = Get-Content $meta -Raw | ConvertFrom-Json } catch { $m = $null }
        if ($m -and $m.capture.sample_count -gt 0) { break }
      }
      $cpu = $p.TotalProcessorTime.TotalSeconds
      $wall = ((Get-Date) - $p.StartTime).TotalSeconds
      if (-not $p.HasExited) { Stop-Process $p }
      $p.WaitForExit()

      if (-not $m -or $m.capture.sample_count -eq 0) {
        Write-Warning "${tag}: the capture did not finish, see $tag.*.log"
        continue
      }
      $iq = Get-ChildItem "data\${tag}_*.iq"
      [pscustomobject]@{
        run        = $run
        rate       = $rate
        fft        = $fft
        samples    = $m.capture.sample_count
        dropped    = $m.capture.dropped_samples
        write_err  = $m.capture.write_error
        size_ok    = $iq.Length -eq 4 * $m.capture.sample_count
        rate_ppm   = [math]::Round(($m.signal.measured_sample_rate_hz / $rate - 1) * 1e6)
        cpu_cores  = [math]::Round($cpu / $wall, 2)
      }
      Remove-Item $iq
    }
  }
}
$rows | Format-Table -AutoSize
$rows | Export-Csv uhd-sweep.csv -NoTypeInformation
