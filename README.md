# VictronBTLELogger

https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html

https://www.victronenergy.com/live/open_source:start

https://github.com/keshavdv/victron-ble

## DBus
I'm using the same dbus connection style to BlueZ I recently learned in my Govee project. This project does not link to bluetooth libraries directly. I've duplicated a couple of functions for handling bluetooth address structures safely.

## OpenSSL
I had to learn to use OpenSSL to decrypt the AES CTR blocks that are being sent by Victron. Writing it up is a good idea. 
