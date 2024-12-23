# rfidcarddeck
RFID Card Deck (using QLab)

This code uses a RFID reader to read NFC tags and send the read data to QLab over WiFi. In this example, the code is used to trigger a set of scenes using QLab's OSC dictionary.
I used the Arduino Uno R4 WiFi to interface with the MFRC522 RFID module, and I added some transistors to control a RGB LED strip.
I used NFC NTAG213 tags, which comply with the MIFARE protocol that the MFRC522 is using.

The repository includes the main sketch and an independent sketch used to write consecutive values to the NFC tags, which speeds up the writing process for a whole deck.

This code was written for the performance El conspirador (o conspiradora), and therefore some parts of the code will be pointless to other projects.
