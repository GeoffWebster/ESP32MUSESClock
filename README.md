Putzeys / Muses72323 / Clock preamplifier control software
===================================

This is an application for controlling a Putzeys / Muses72323 preamplifier. Items covered include
*  Source selection
*  Volume control
*  Mute
*  Visual display of above settings
*  Storage of current settings
*  Connection to WiFi
*  Visual display of clock synched to NTP server

An ESP32-DevKitC V4 interfaces through a motherboard to the on-board rotary encoder, IR receiver and 320x240 TFT display. External interfaces connect to a source select relay board and balanced digital volume controller / preamplifier board (based on Bruno Putzeys balanced pre-amp with an integrated MAS6116 digital volume control chip).

The Quadrature rotary encoder/switch (PEC11R) provides control of the volume as the default mode. Pressing the encoder shaft button switches over to SOURCE SELECT mode and this remains as the active mode till rotary encoder turning is inactive for longer than the TIME_EXITSELECT value (in seconds).

An Infra-red receiver module (TSOP4836) provides remote control of volume level, balance, mute, source select and display on/off. RC5 protocol was chosen as I had a remote transmitter from my existing preamplifier using that code. It's also the protocol used by many freely available transmitters.

The MUSES72323 stereo digital volume control provides independently programmable gain of each channel from -111.75dB to 0dB together with mute. Communication between the ESP32 and the MUSE72323 is via an SPI bus.

The 320x240 TFT display with an SPI interface provides visual data for source input selected, Attenuator setting (-111.75dB to 0dB), mute status and digital clock.

Code Libraries
=================
Interfacing with the TFT display, source selector, IR decoder, rotary encoder/switch, WiFi and NTP uses shared libraries. These are
* TFT_eSPI (PlatformIO libdeps value = bodmer/TFT_eSPI@^2.3.70)
* RC5    https://github.com/guyc/RC5
* ESP32RotaryEncoder (PlatformIO libdeps value = maffooclock/ESP32RotaryEncoder@^1.1.0)
* MCP23S08 SPI bus expander for source selector (PlatformIO libdeps value = robtillaart/MCP23S08@^0.4.0)
* MUSES72323    https://github.com/GeoffWebster/Muses72323

The MUSES72323 library is an adaptation of the MUSES72320 libray by Christoffer Hjalmarsson (which can be found [here](https://github.com/qhris/Muses72320) ).
