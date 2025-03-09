# My redis

This repository aims to implement an in-memory key-value store, primarily used for caching. The project is written in C++ to allow efficient implementation of fundamental data structures, such as hash tables.


## Documentation. Important concepts needed for the project.

#### Extension: Difference between IPv4 and IPv6. 

This table sums up the differences between those two IPs. (Source: [GeeksForGeeks](https://www.geeksforgeeks.org/differences-between-ipv4-and-ipv6/))

| IPv4 | IPv6 |
|------|------|
| IPv4 has a 32-bit address length | IPv6 has a 128-bit address length |
| It Supports Manual and **DHCP** address configuration | It supports Auto and renumbering address configuration |
| In IPv4 end to end, connection integrity is Unachievable | In IPv6 end-to-end, connection integrity is Achievable |
| It can generate 4.29×10⁹ address space | The address space of IPv6 is quite large; it can produce 3.4×10³⁸ address space |
| The Security feature is dependent on the application | **IPSEC** is an inbuilt security feature in the IPv6 protocol |
| Address representation of IPv4 is in decimal | Address representation of IPv6 is in hexadecimal |
| **Fragmentation** performed by Sender and forwarding routers | In IPv6 fragmentation is performed only by the sender |
| In IPv4 Packet flow identification is not available | In IPv6 packet flow identification is Available and uses the flow label field in the header |
| In IPv4 checksum field is available | In IPv6 **checksum** field is not available |
| It has a broadcast Message Transmission Scheme | In IPv6 multicast and anycast message transmission scheme is available |
| In IPv4 Encryption and Authentication facility not provided | In IPv6 **Encryption** and Authentication are provided |
| IPv4 has a header of 20-60 bytes. | IPv6 has a header of 40 bytes fixed |
| IPv4 can be converted to IPv6 | Not all IPv6 can be converted to IPv4 |
| IPv4 consists of 4 fields which are separated by addresses dot (.) | IPv6 consists of 8 fields, which are separated by a colon (:) |
| IPv4's IP addresses are divided into five different classes: Class A, Class B, Class C, Class D, Class E. | IPv6 does not have any classes of the IP address. |
| IPv4 supports VLSM (**Variable Length subnet mask**) | IPv6 does not support VLSM. |
| **Example of IPv4**: 66.94.29.13 | **Example of IPv6**: 2001:0000:3238:DFE1:0063:0000:0000:FEFB |
