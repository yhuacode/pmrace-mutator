#!/bin/bash

max_trial_times=40

for seed in out_$1/default/crashes/* 
do 
  cnt=0
  for i in $(seq 1 $max_trial_times)
  do
    (./dump-$1 $seed $2 | ~/ck/$1 $1-pool $2) 1>/dev/null 2>&1
    if [ $? -ne 0 ] 
    then
      let cnt++
    fi
  done

  if [[ $cnt -eq 0 || $cnt -eq 1 ]]
  then
    printf "$seed\033[100G  %s\n" "$cnt crash out of $max_trial_times runs"
  else
    printf "$seed\033[100G  %s\n" "$cnt crashes out of $max_trial_times runs"
  fi
done
