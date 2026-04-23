#include "TC74.h"

TC74::TC74(uint8_t adr){
  _adr = adr;
}

void TC74::begin(TwoWire &wire){
  _wire = &wire;
  _wire->begin();
}

float TC74::readTemperature(char unit){
  int8_t val; //easiest way for 2's complement
  _wire->beginTransmission(_adr);
  _wire->write(0x00); //I NEED TEMPERATURE
  _wire->endTransmission(false);
  _wire->requestFrom(_adr, byte(1));
  if(_wire->available()){
    val = _wire->read();
    _wire->endTransmission();
  } else {
    _wire->endTransmission();
    return -998; //device not found
  }

  switch(unit){
	  case 'c':
	  case 'C':
		return float(val);
	  case 'k':
	  case 'K':
		return float(float(val) + 273.15);
	  case 'f':
	  case 'F':
		return float((float(val)*(9.0/5)) + 32);
	  default:
		return -999;
  }
  
}

void TC74::TC74Mode(bool mode){
  _wire->beginTransmission(_adr);
  _wire->write(0x01); //R/W Config
  _wire->write(0x00 | (mode << 7)); //D[7] -> STANDBY switch
  _wire->endTransmission();
}

bool TC74::isStandby(){
  _wire->beginTransmission(_adr);
  _wire->write(0x01);
  _wire->endTransmission(false);
  _wire->requestFrom(_adr,byte(1));
  if(_wire->available()){
    if(_wire->read() == 0x40 || _wire->read() == 0x00){ //0x00 or 0x40
      _wire->endTransmission();
      return false;
    } else {
      _wire->endTransmission();
      return true;
    }
  }
}
