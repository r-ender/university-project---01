# routing - university-project-using-DPDK--01
A small project about routing based on the DPDK-framework part 1

The setup consists of 4 remotely running Linux-based virtual machines that were accessed via SSH. One VM is the router and the remaining three are the clients. The configuration of the Ethernet and IP-settings are done in client_1.sh, client_2.sh, client_3.sh and router.sh

This project contains an implementation of a CLI, IP-Header validation check, extraction of the destination IP-address and the implementation of ARP-replies in router01.c. In routing_table01.c the routing table was built according to the DIR-24-8-BASIC described by Gupta. Those files were included in a given framework which I won't upload because it wasn't my work. But if somebody is interested in executing the whole project, get in touch with me and we'll find a solution :-)  
