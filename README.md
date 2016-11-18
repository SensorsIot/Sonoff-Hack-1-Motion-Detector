# Sonoff-Hack-1-Motion-Detector
Sonoff Hack #1: Proximity/Motion Detector

Code used in video: https://www.youtube.com/watch?v=6fWFnxh0EiQ

Two sketches, a senser and a receiving part. The receiver is for a standard Sonoff device, the sender can be used on any NodeMCU compatible board
GPIO0 / D3 is used to switch between setup and run mode. In setup, a WLAN with the name of SONOFF and the address 192.168.4.1 is created.
Constant5 and 6 of the sender have to be filled with the MDNS names of the receiving devices.
