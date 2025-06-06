#!/bin/bash
# perf all metrics test
# SPDX-License-Identifier: GPL-2.0

ParanoidAndNotRoot()
{
  [ "$(id -u)" != 0 ] && [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt $1 ]
}

test_prog="sleep 0.01"
system_wide_flag="-a"
if ParanoidAndNotRoot 0
then
  system_wide_flag=""
  test_prog="perf test -w noploop"
fi

err=0
for m in $(perf list --raw-dump metrics); do
  echo "Testing $m"
  result=$(perf stat -M "$m" $system_wide_flag -- $test_prog 2>&1)
  result_err=$?
  if [[ $result_err -eq 0 && "$result" =~ ${m:0:50} ]]
  then
    # No error result and metric shown.
    continue
  fi
  if [[ "$result" =~ "Cannot resolve IDs for" ]]
  then
    echo "Metric contains missing events"
    echo $result
    err=1 # Fail
    continue
  elif [[ "$result" =~ \
        "Access to performance monitoring and observability operations is limited" ]]
  then
    echo "Permission failure"
    echo $result
    if [[ $err -eq 0 ]]
    then
      err=2 # Skip
    fi
    continue
  elif [[ "$result" =~ "in per-thread mode, enable system wide" ]]
  then
    echo "Permissions - need system wide mode"
    echo $result
    if [[ $err -eq 0 ]]
    then
      err=2 # Skip
    fi
    continue
  elif [[ "$result" =~ "<not supported>" ]]
  then
    echo "Not supported events"
    echo $result
    if [[ $err -eq 0 ]]
    then
      err=2 # Skip
    fi
    continue
  elif [[ "$result" =~ "<not counted>" ]]
  then
    echo "Not counted events"
    echo $result
    if [[ $err -eq 0 ]]
    then
      err=2 # Skip
    fi
    continue
  elif [[ "$result" =~ "FP_ARITH" || "$result" =~ "AMX" ]]
  then
    echo "FP issues"
    echo $result
    if [[ $err -eq 0 ]]
    then
      err=2 # Skip
    fi
    continue
  elif [[ "$result" =~ "PMM" ]]
  then
    echo "Optane memory issues"
    echo $result
    if [[ $err -eq 0 ]]
    then
      err=2 # Skip
    fi
    continue
  fi

  # Failed, possibly the workload was too small so retry with something longer.
  result=$(perf stat -M "$m" $system_wide_flag -- perf bench internals synthesize 2>&1)
  result_err=$?
  if [[ $result_err -eq 0 && "$result" =~ ${m:0:50} ]]
  then
    # No error result and metric shown.
    continue
  fi
  echo "Metric '$m' has non-zero error '$result_err' or not printed in:"
  echo "$result"
  err=1
done

exit "$err"
