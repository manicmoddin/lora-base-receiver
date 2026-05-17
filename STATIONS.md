# Station information
The stations that are being used can be classified by types

* Base Stations
* Roaming GPS Stations
* Environment Stations
  
  ## Base Stations
  Base stations are fixed location and trasform the information from one type to another.
  ID options: A0 - AF;

  Eg, Home BASE station - HTTP --> LoRa / LoRa --> HTTP

  So in this example the home base station will accept an HTTP Api endpoint and will convert this to LoRa and vicea versa.

  ## Roming GPS stations
  Thes stations are portable and can be used in cars, when walking etc. These should only be used with GPS info.
  ID Options: B0 - BF

  ## Control Stations
  These stations control something:
  ID Options: C0 - CF

  ## Environment Stations:
  Thes stations take environmental readings and send over Lora.
  ID Options: D0 - DF