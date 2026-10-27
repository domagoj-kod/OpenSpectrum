#!/usr/bin/env bash
# Sweep FFT sizes for one binary; log FRAME-TIMING + perf counters per run.
#
#   ./bench.sh <binary> [tag]
#   ./bench.sh ./OpenSpectrum-v3.9.0-x86_64.AppImage v3.9.0
#   ./bench.sh ./openspectrum v3.9.1
#
# Run it once per binary, same box, same session, then diff the tags. Every A/B
# in this repo that went wrong went wrong because the two halves were measured
# under conditions nobody wrote down -- so the header block below is the point
# of this script as much as the numbers are.
#
# Env: SIZES REPS DUR SETTLE CAP OUT EVENTS GOVERNOR
set -uo pipefail

BIN=${1:?usage: ./bench.sh <binary> [tag]}
TAG=${2:-$(basename "$BIN" | sed 's/\.AppImage$//;s/^openspectrum$/local/')}
CAP=${CAP:-data/capture_20260707T105702Z_92.600MHz.iq}
SIZES=${SIZES:-"1024 2048 4096 8192 16384 32768 65536"}
REPS=${REPS:-2}
DUR=${DUR:-40}
SETTLE=${SETTLE:-5}
OUT=${OUT:-logs}

# 4 general-purpose counters max -> fits the PMU with HT *on* (4/thread) and
# leaves headroom with HT off (8/thread), so nothing multiplexes. cycles and
# instructions ride Intel's fixed-function counters and are free; task-clock /
# page-faults / context-switches / cpu-migrations are software events and are
# also free. If perf prints a (NN%) next to an event, it multiplexed and
# extrapolated -- that number is an estimate, not a count. `-d -d` guarantees it.
EVENTS=${EVENTS:-task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,L1-dcache-load-misses,LLC-load-misses}

[[ -x $BIN ]] || { echo "not executable: $BIN" >&2; exit 1; }
[[ -r $CAP ]] || { echo "capture not readable: $CAP" >&2; exit 1; }
mkdir -p "$OUT"

# --- optional governor pin -------------------------------------------------
# The governor is the single biggest confound on this workload: --max-fps 30
# leaves the CPU ~97% idle, so it sits at its lowest P-state and `cpu` becomes
# work / whatever-clock-it-picked. Pin it to compare CODE; leave it alone to
# measure what users actually get. Do both if you care.
GOV_RESTORE=""
gov_files() { ls /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null; }
if [[ -n ${GOVERNOR:-} ]]; then
  mapfile -t GF < <(gov_files)
  if ((${#GF[@]})); then
    GOV_RESTORE=$(cat "${GF[0]}")
    echo "==> pinning governor: $GOV_RESTORE -> $GOVERNOR (sudo)"
    for f in "${GF[@]}"; do echo "$GOVERNOR" | sudo tee "$f" >/dev/null; done
    trap 'for f in $(gov_files); do echo "$GOV_RESTORE" | sudo tee "$f" >/dev/null; done; echo "==> governor restored to $GOV_RESTORE"' EXIT
  else
    echo "!! no cpufreq interface; GOVERNOR ignored" >&2
  fi
fi

# --- perf availability -----------------------------------------------------
PERF=1
command -v perf >/dev/null || { echo "!! perf not found -- logging FRAME-TIMING only" >&2; PERF=0; }
if ((PERF)); then
  if ! perf stat -e cycles -o /dev/null true 2>/dev/null; then
    PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "?")
    echo "!! perf denied (perf_event_paranoid=$PARANOID)." >&2
    echo "   Fix WITHOUT sudo-ing the app (sudo drops \$DISPLAY/\$XAUTHORITY and" >&2
    echo "   SDL then cannot open a window):  sudo sysctl -w kernel.perf_event_paranoid=1" >&2
    echo "   Continuing with FRAME-TIMING only." >&2
    PERF=0
  fi
fi

# --- package energy (RAPL) -------------------------------------------------
# Intel's package counter (cores + iGPU) covers the whole box, not this process,
# so each rep also meters an idle stretch of the same length to subtract.
# energy_uj is root-only since CVE-2020-8694, hence sudo.
RAPL=/sys/class/powercap/intel-rapl:0
ENERGY=0
if E_MAX=$(cat "$RAPL/max_energy_range_uj" 2>/dev/null) && sudo cat "$RAPL/energy_uj" >/dev/null; then
  ENERGY=1
else
  echo "!! no RAPL counter -- logging without energy" >&2
fi
# Runs "$@"; appends "label joules seconds watts" to the energy log, sets POW.
POW=""
metered() {
  local label=$1 e0 t0 e1 t1
  shift
  ((ENERGY)) || { "$@"; return; }
  e0=$(sudo cat "$RAPL/energy_uj"); t0=$(date +%s%N)
  "$@"
  t1=$(date +%s%N); e1=$(sudo cat "$RAPL/energy_uj")
  # The counter wraps at max_energy_range_uj.
  awk -v l="$label" -v e=$(( (e1 - e0 + E_MAX) % E_MAX )) -v t=$(( t1 - t0 )) \
    'BEGIN{printf "%s %.1f %.2f %.2f\n", l, e/1e6, t/1e9, e*1e3/t}' >> "$OUT/$TAG-energy.txt"
  POW="  $(tail -n1 "$OUT/$TAG-energy.txt" | cut -d' ' -f4) W"
}

# --- provenance ------------------------------------------------------------
HDR="$OUT/$TAG-conditions.txt"
{
  echo "tag:        $TAG"
  echo "date:       $(date -Is)"
  echo "binary:     $BIN ($(stat -c%s "$BIN") bytes)"
  echo "commit:     $(git rev-parse --short HEAD 2>/dev/null || echo n/a) $(git diff --quiet 2>/dev/null || echo '(DIRTY)')"
  echo "capture:    $CAP"
  echo "cpu:        $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | xargs)"
  echo "cores:      $(nproc) logical"
  echo "smt:        $(cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo unknown)"
  echo "governor:   $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  echo "driver:     $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null || echo unknown)"
  echo "epp:        $(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null || echo unknown)"
  echo "profile:    $(cat /sys/firmware/acpi/platform_profile 2>/dev/null || echo unknown)"
  echo "on AC:      $(cat /sys/class/power_supply/A*/online 2>/dev/null || echo unknown)"
  echo "kernel:     $(uname -r)"
  echo "session:    ${XDG_SESSION_TYPE:-unknown}"
  echo "perf:       $( ((PERF)) && echo yes || echo no )"
  echo "energy:     $( ((ENERGY)) && echo "RAPL package, $TAG-energy.txt" || echo no )"
  echo "events:     $EVENTS"
  echo "sweep:      sizes=[$SIZES] reps=$REPS dur=${DUR}s settle=${SETTLE}s"
} | tee "$HDR"
echo

# Warm the page cache so run #1 isn't paying disk I/O the others don't.
# Matters most right after a reboot, which is exactly when you'll run this.
echo "==> warming page cache for $CAP"
cat "$CAP" > /dev/null

# Reps are the OUTER loop: each rep sweeps every size, so thermal drift spreads
# across all sizes instead of landing on whichever ran last.
for rep in $(seq 1 "$REPS"); do
  if ((ENERGY)); then
    printf '==> %-10s idle        rep=%s ... ' "$TAG" "$rep"
    metered "idle-r$rep" sleep "$DUR"
    echo "${POW#  }"
    sleep "$SETTLE"
  fi
  for n in $SIZES; do
    base="$OUT/$TAG-$n-r$rep"
    printf '==> %-10s fft=%-6s rep=%s ... ' "$TAG" "$n" "$rep"
    if ((PERF)); then
      metered "$n-r$rep" perf stat -e "$EVENTS" -o "$base-perf.log" \
        -- timeout -s INT "$DUR" "$BIN" --play "$CAP" --fft-size "$n" \
        > "$base.log" 2>&1
    else
      metered "$n-r$rep" timeout -s INT "$DUR" "$BIN" --play "$CAP" --fft-size "$n" > "$base.log" 2>&1
    fi
    # SIGINT so the app shuts down cleanly and flushes its log; SIGKILL truncates
    # it. timeout exits 124 on the kill -- that is success here, not a failure.
    cpu=$(grep -o 'cpu avg=[0-9.]*' "$base.log" | tail -n +4 | sed 's/.*=//' \
          | sort -n | awk '{a[NR]=$1} END{if(NR)printf "%.2f", a[int(NR/2)+1]; else print "?"}')
    win=$(grep -c FRAME-TIMING "$base.log")
    mux=$( ((PERF)) && grep -c '(\([0-9.]\+%\))' "$base-perf.log" || true)
    printf 'cpu=%-6s windows=%-3s%s%s\n' "$cpu" "$win" "$POW" \
      "$( (( mux > 0 )) && echo "  !! $mux events MULTIPLEXED" )"
    sleep "$SETTLE"
  done
done

echo
echo "==> done. logs in $OUT/$TAG-*.log, conditions in $HDR"
