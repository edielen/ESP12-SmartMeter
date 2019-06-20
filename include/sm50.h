#include <WString.h>

namespace sm50
{

class datagram
{
public:
    datagram();

    void reset();
    void add(char c);
    const String asString() const;

    static long mEVLT; // consumption low tariff (0,001kWh)
    static long mEVHT; // consumption high tariff (0,001kWh)
    static long mEPLT; // production low tariff (0,001kWh)
    static long mEPHT; // production high tariff (0,001kWh)
    static long mEAV;  // actual consumption (0,001kW)
    static long mEAP;  // actual production (0,001kW)
    static long mCT;   // actual tariff (1/2)
    static long mGVT;  // m-bus reading gas (0,001m3)
    static long mWVT;  // m-bus reading water (0,001m3)
    static long oEVLT; // consumption low tariff (0,001kWh)
    static long oEVHT; // consumption high tariff (0,001kWh)
    static long oEPLT; // production low tariff (0,001kWh)
    static long oEPHT; // production high tariff (0,001kWh)
    static long oGVT;  // m-bus reading gas (0,001m3)
    static long oWVT;  // m-bus reading water (0,001m3)

private:
    String strDatagram;
};

class crc16
{
public:
    crc16();

    void reset();
    void update(unsigned char c);
    bool validate(const String base) const;
    const String asHexString() const;

private:
    uint16_t crc;
};

class decode
{
public:
};

} // namespace sm50