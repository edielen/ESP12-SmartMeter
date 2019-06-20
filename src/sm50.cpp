#include "sm50.h"

long sm50::datagram::mEVLT = 0; // consumption low tariff (0,001kWh)
long sm50::datagram::mEVHT = 0; // consumption high tariff (0,001kWh)
long sm50::datagram::mEPLT = 0; // production low tariff (0,001kWh)
long sm50::datagram::mEPHT = 0; // production high tariff (0,001kWh)
long sm50::datagram::mEAV = 0;  // actual consumption (0,001kW)
long sm50::datagram::mEAP = 0;  // actual production (0,001kW)
long sm50::datagram::mCT = 0;   // actual tariff (1/2)
long sm50::datagram::mGVT = 0;  // m-bus reading gas (0,001m3)
long sm50::datagram::mWVT = 0;  // m-bus reading water (0,001m3)
long sm50::datagram::oEVLT = 0; // consumption low tariff (0,001kWh)
long sm50::datagram::oEVHT = 0; // consumption high tariff (0,001kWh)
long sm50::datagram::oEPLT = 0; // production low tariff (0,001kWh)
long sm50::datagram::oEPHT = 0; // production high tariff (0,001kWh)
long sm50::datagram::oGVT = 0;  // m-bus reading gas (0,001m3)
long sm50::datagram::oWVT = 0;  // m-bus reading water (0,001m3)

sm50::datagram::datagram()
{
    this->reset();
}

void sm50::datagram::add(char c)
{
    this->strDatagram += (String) c;
}

void sm50::datagram::reset()
{
    this->strDatagram = "";
}

String const sm50::datagram::asString() const
{
    return this->strDatagram;
}