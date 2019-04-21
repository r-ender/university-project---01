#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_arp.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_byteorder.h>
#include <rte_launch.h>

#include <arpa/inet.h>
#include <rte_cycles.h>
#include "dpdk_init.h"
#include "routing_table.h"
#include "router.h"

#include <sys/socket.h>
#include <netdb.h>      

//struct for routes, ip-address and port
typedef struct {
	char ip_addr[16];
	unsigned int id_port;
}port_t;

typedef struct {
	char mac_addr[18];
	unsigned int dest_if_id;
}next_hop_t;

typedef struct {
	char prefix_ip[19];
	next_hop_t next_hop;
}route_t;

struct thread_t {
    unsigned int device_id;
    unsigned int rx_queue;
    unsigned int tx_queue;
    unsigned long ip_address;
    unsigned int thread_nr;
};


port_t port[10];
route_t route[10];

//counting routes/ports
unsigned int nr_routes = 0; 
unsigned int nr_ports = 0;


bool check_ipv4_addr(char *ip_addr)
{
    struct addrinfo *temp=NULL;
    bool flag = false;
    struct addrinfo addr;

    memset(&addr, '\0', sizeof(struct addrinfo));
    addr.ai_family = PF_UNSPEC;
    addr.ai_flags = AI_NUMERICHOST;

    if (getaddrinfo(ip_addr, NULL, &addr, &temp)) return false;
   
    if(temp->ai_family != AF_INET) {
        flag = false;
    } 
    else 
    {
       flag = true;
    }
   freeaddrinfo(temp);
   return flag;
}

bool check_nw_mask(char *nw_mask, char *ip_addr)
{
    unsigned int mask = 0;
    if(ip_addr == NULL)
        return false;

    if(nw_mask == NULL)
    {
	mask = 32; //assume 32 in undefined cases
    }
    else
    {
        mask = atoi(nw_mask);
         if(mask > 32)
         {
            return false;
         }
    }
    if(!check_ipv4_addr(ip_addr))
        return false;
    return true;
}

unsigned int calc_csum(struct ipv4_hdr *hdr)
{
	//calculate sum of header excluding checksum-field itself
	unsigned int csum=0;
	unsigned int *temp_hdr = (unsigned int *) hdr;
	for(unsigned int i = 0; i<10; i++)
	{
		if(i!=5){
			csum += temp_hdr[i];
		}
	}

	//make 2bytes of of 4bytes and one-complement
	csum = (csum & 0xffff) + (csum >> 0b10000);
	csum = (csum & 0xffff) + (csum >> 0b10000);
	csum = (~csum) & 0x0000ffff;

	if (csum == 0)
	{
		return 0xffff;
	}
	else
	{
		return csum;
	}
}

bool ip_validation(struct rte_mbuf* buffer)
{
	puts("ip validation\n");
	struct ipv4_hdr *ip_hdr;
	unsigned int len_pkt = 0, csum = 0;

	len_pkt = buffer->data_len;
	ip_hdr = rte_pktmbuf_mtod_offset(buffer,struct ipv4_hdr*,sizeof(struct ether_hdr));

	if((ip_hdr->version_ihl & 0b11110000) != 4)
	{
		puts("IP-version error!\n");
		return false;
	}

	//check size of packet
	if(len_pkt < sizeof(struct ether_hdr)+sizeof(struct ipv4_hdr))
	{
		puts("Packet-length error!\n");
		return false;
	}

	csum = ip_hdr->hdr_checksum;
	if(csum != calc_csum(ip_hdr))
	{
		puts("Header-Checksum error!\n");
		return false;
	}

	if((ip_hdr->version_ihl & 0b00001111) < 5)
	{
		puts("Header-length error!\n");
		return false;
	}

	if(rte_cpu_to_be_16(ip_hdr->total_length) < 20)
	{
		puts("Packet-length error!\n");
		return false;
	}

	return true;
}

/*
bool get_ipv4(struct thread_t* thread)
{
    unsigned int temp = 0, part[4] = {0};
    unsigned long temp_ipaddr = 0;
    char *addr = port[thread->device_id].ip_addr;

    while (*addr) {

        if (!(isdigit((unsigned char)*addr))) 
	{
	    ++temp;
            
        } 
	else 
	{
	    part[temp] = 10*part[temp];
            part[temp] = part[temp]+( *addr - '0');
        }
        addr++;
    }

    temp_ipaddr = rte_cpu_to_be_32(IPv4(part[0],part[1],part[2],part[3]));
    if(temp_ipaddr != 0)
    {
        thread->ip_address = temp_ipaddr;
        return true;
    }
    return false;
}
*/ 

void fwd_packet(struct thread_t* thread,struct rte_mbuf* buffer)
{
    struct arp_ipv4* arpdata;
    struct arp_hdr* arphdr;
    struct routing_table_entry* entry;
    struct ipv4_hdr* ip_hdr;
    struct ether_hdr* ether;
    

    ether = rte_pktmbuf_mtod(buffer, struct ether_hdr*);

    if(rte_cpu_to_be_16(ether->ether_type) == 0x0806)
    {
        arphdr = rte_pktmbuf_mtod_offset(buffer,struct arp_hdr*,sizeof(struct ether_hdr));
        arpdata = rte_pktmbuf_mtod_offset(buffer,struct arp_ipv4*,sizeof(struct ether_hdr) + 8);

        //ARP reply
        if(arpdata->arp_tip == thread->ip_address)
        {
            puts("Send out ARP-reply\n");
            arphdr->arp_op = 0x200; 
            arpdata->arp_tip = arpdata->arp_sip;
	    entry = get_next_hop(arpdata->arp_tip);
            arpdata->arp_sip = thread->ip_address;
            memcpy(&arpdata->arp_tha,&arpdata->arp_sha,6);
            memcpy(&arpdata->arp_sha,&entry->dst_mac,6);
        }
        else
        {
            entry = get_next_hop(arpdata->arp_tip);
            if(entry ==  NULL)
            {
                puts("no destination specified --> free memory!\n");
		rte_mbuf_raw_free(buffer);
		return;
            }
        }
        while (!rte_eth_tx_burst(entry->dst_port,entry->dst_port, &buffer, 1));
    }

    else if(rte_cpu_to_be_16(ether->ether_type) == 0x0800)
    {
        ip_hdr = rte_pktmbuf_mtod_offset(buffer,struct ipv4_hdr*,sizeof(struct ether_hdr));
        if(!ip_validation(buffer))
        {
            printf("wrong ip-packet  --> free memory!\n");
            rte_mbuf_raw_free(buffer);
            return;
        }
        entry = get_next_hop(ip_hdr->dst_addr);
        if(entry ==  NULL)
        {
            printf("no destination specified --> free memory!\n");
            rte_mbuf_raw_free(buffer);
            return;
        }
	while (!rte_eth_tx_burst(entry->dst_port,thread->tx_queue, &buffer, 1));
    }
}

int router_thread(void* arg) {
    struct thread_t* thread = (struct thread_t*) arg;
    printf("thread-Nr: %d\n",thread->thread_nr);
    struct rte_mbuf* buffer[64];

    while(true)
    {
        //Receive packets
        uint32_t rx = recv_from_device(thread->device_id,3, buffer, 64);
        if (!rx) usleep(100);

        for (uint16_t i = 0; i < rx; ++i) 
	{
	 	fwd_packet(thread, buffer[i]);
	}
    }
    return 0;
}


//router thread configuration
void start_thread(uint8_t port_id) {

	switch(port_id){
		case 0:{
		    struct thread_t* thread0 = (struct thread_t*) malloc(sizeof(struct thread_t));
		    thread0->device_id = 0;
		    thread0->rx_queue = 0;
		    thread0->tx_queue = 0;
		    thread0->ip_address = rte_cpu_to_be_32(IPv4(10,0,0,1));
		    printf("\nPort 0: %d.%d.%d.%d\n",(int)thread0->ip_address & 0xff, (int)thread0->ip_address >> 8 & 0xff, (int)thread0->ip_address >> 16 & 0xff,(int)thread0->ip_address >> 24 & 0xff);
		    thread0->thread_nr=0;
		    rte_eal_remote_launch(router_thread, thread0, 1);
		    break;
		}
		case 1:{
		    
		    struct thread_t* thread1 = (struct thread_t*) malloc(sizeof(struct thread_t));
		    thread1->device_id = 1;
		    thread1->rx_queue = 0;
		    thread1->tx_queue = 1;
		    thread1->ip_address = rte_cpu_to_be_32(IPv4(192,168,0,1));
		    printf("Port 1: %d.%d.%d.%d\n",(int)thread1->ip_address & 0xff, (int)thread1->ip_address >> 8 & 0xff, (int)thread1->ip_address >> 16 & 0xff,(int)thread1->ip_address >> 24 & 0xff);
		    thread1->thread_nr = 1;
	 	    rte_eal_remote_launch(router_thread, thread1, 2);
		    break;
		}
	 
		case 2:  { 
		    struct thread_t* thread2 = (struct thread_t*) malloc(sizeof(struct thread_t));
		    thread2->device_id = 2;
		    thread2->rx_queue = 0;
		    thread2->tx_queue = 2;
		    thread2->ip_address = rte_cpu_to_be_32(IPv4(172,16,0,1));
		    printf("Port 2: %d.%d.%d.%d\n",(int)thread2->ip_address & 0xff, (int)thread2->ip_address >> 8 & 0xff,(int)thread2->ip_address >> 16 & 0xff,(int)thread2->ip_address >> 24 & 0xff);
		    thread2->thread_nr = 2;
		    rte_eal_remote_launch(router_thread, thread2, 3);
		    break;
		}
		default:
		{
			puts("error!\n");
			break;
		}
	}
}

int parse_args(int argc, char **argv) {
	unsigned int router_if_id,nexthop_if_id;
	char *router_ip,*nexthop_mac, *network, *ip_addr, *nw_mask;
	int opt;
	size_t nwlen=0, len = 0, iplen=0;
        while ((opt = getopt(argc, argv, "p:r:")) != EOF) {
                switch (opt) {
                        case 'p':
				puts("\n\nPort added:");
				router_if_id = atoi(optarg);
				if(router_if_id > 2){
					puts("Maximum number of ports is 3 (from 0 to 2) !!\n");
				}else{
					len = strlen(optarg) - 2;
					router_ip = (char *)calloc(len,sizeof(char));
					strncpy(router_ip,&optarg[2],len);

					strcpy(port[nr_ports].ip_addr,router_ip);
					printf("port.ip_addr: %s\n", port[nr_ports].ip_addr);
					if(!check_ipv4_addr(port[nr_ports].ip_addr)) puts("wrong Port-IPv4!\n");
					port[nr_ports].id_port = router_if_id;
					printf("port.id_port: %d\n", port[nr_ports].id_port);
					nr_ports++;
				}
				break;

                        case 'r':
                           	puts("\n\nRoute added:");
				nwlen = strlen(optarg) - 20;
				network = (char *)calloc(nwlen,sizeof(char));
				strncpy(network,&optarg[0],nwlen);
				iplen = nwlen-3;
				ip_addr = (char *)calloc(iplen,sizeof(char));
				strncpy(ip_addr,&optarg[0],iplen);
				if(!check_ipv4_addr(ip_addr)) puts("wrong Route-IPv4!\n");

				nw_mask = (char *)calloc(nwlen-iplen-1,sizeof(char));
				strncpy(nw_mask,&optarg[iplen+1],nwlen-iplen-1);

				if(!check_nw_mask(nw_mask,ip_addr)) puts("Network-mask wrong!\n");

				size_t maclen = 17;
				nexthop_mac = (char *)calloc(maclen,sizeof(char));
				strncpy(nexthop_mac,&optarg[nwlen+1],maclen);
				nexthop_if_id = atoi(&optarg[strlen(optarg)-1]);

				strcpy(route[nr_routes].prefix_ip, ip_addr);
				strcpy(route[nr_routes].next_hop.mac_addr, nexthop_mac);
				route[nr_routes].next_hop.dest_if_id = nexthop_if_id;
				printf("route.prefix_ip: %s\n", route[nr_routes].prefix_ip);
				printf("next_hop.mac: %s\n", route[nr_routes].next_hop.mac_addr);
				printf("next_hop.if_id: %d\n", route[nr_routes].next_hop.dest_if_id);
 				nr_routes++;

				break;

                        default:
                                puts("Only ports (-p Port-ID,IP-Addr.) and routes (-r Network,MAC-Addr.,Dest-ID) can be configured !!!\n");
                                return -1;
                }
        }


    return 1;
}

