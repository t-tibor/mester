## Important files

- 01_device_tree_overlays: Overlays for pin configuration and for dmtimer/ecap setups.
- 02_kernel_patching: New kernel installer script.
- 03_measurements: Measurement data evaluation scripts.
- 04_rtio: time servo, timekeeper, (n)PPS generator, event capturer source code
- PREEMPT_RT: rt patch specific files
- real_time_sched: periodis rt task compatible with normal and RT patched kernels
- xenomai: xenomai specific files, periodic xenomai task

## Capabilities
- Generate PPS and nPPS signals on DMTimer4/6/7 outputs.
- Timestamp external events using eCAP0/eCAP2/DMTimer5/DMTimer6/DMTimer7.

## SD image
- Image location can be found in: SD_image_location.txt.

## Image usage
- PPS generator and event capturer: /home/debian/PPS_generator.
- Run it as super user: "sudo ~/PPS_generator".
- PPS pin: P8_08 (DMTimer 7 portimerpwm output).
- Input capture pins: P9_42 (eCAP0) and P9_28 (eCAP2).
- Use command line switch -i to enable the input capture functionality.
- Timestamps are witten to "icap_channel_0.log" and "icap_channel_2.log"
- Use -l switch to write the timestamps to stdout.
- Source code of the PPS_generator can be found under /04_rtio/uspace/pps_generator.c

