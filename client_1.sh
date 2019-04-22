#!/bin/bash                                                                     
ip link set eth1 down                                                           
ip addr add 10.0.0.2/24 dev eth1                                                
ip link set eth1 up                                                             
                       
ip route add 192.168.0.2 via 10.0.0.1 dev eth1                    
ip route add 172.16.0.2 via 10.0.0.1 dev eth1
