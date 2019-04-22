//project problem 3

#include <rte_config.h>
#include <rte_ip.h>
#include "routing_table.h"

//struct for new entry according to add_route
typedef struct
{
    uint8_t prefix;
    uint8_t port;
    uint32_t ip_addr;
} new_entry_t;

//struct for TBL24
typedef struct
{
    uint8_t next_hop;
    uint8_t msb;
} TBL24_entry_t;

//allocate enough memspace for router-interfaces
struct ether_addr MAC[200];

//struct for next hop
struct routing_table_entry next_hop;


//Value of 2^24, these many entries need to be present in the TBL24.
#define sizeTBL24 16777216
#define sizeTBLlong 40000

//initialize structs with defined values
TBL24_entry_t TBL24_entry[sizeTBL24];
uint8_t TBLlong[sizeTBLlong];

//allocate enough memspace
new_entry_t new_entry[200];
uint8_t new_entry_nr = 0;


void add_route(uint32_t ip_addr, uint8_t prefix, struct ether_addr* mac_addr, uint8_t port)
{
    new_entry[new_entry_nr].port = port;
    new_entry[new_entry_nr].prefix = prefix;
    new_entry[new_entry_nr].ip_addr = ip_addr;
    ++new_entry_nr;

    //for routing_table_entry
    memcpy(&MAC[port],mac_addr,sizeof(struct ether_addr));
}

struct routing_table_entry* get_next_hop(uint32_t ip)
{
    //cut out the last 8 bits
    uint32_t addr_24 = ip >> 8; 
    if(TBL24_entry[addr_24].msb == (uint8_t)false || TBL24_entry[addr_24].msb == (uint8_t)true) //normally not recommended but here msb is only 0 or 1
    {

	if(TBL24_entry[addr_24].msb == (uint8_t)true)
        {
            addr_24 = (TBL24_entry[addr_24].next_hop << 8) + (ip & 0xff);
            next_hop.dst_port = TBLlong[addr_24];
        }
        else if(TBL24_entry[addr_24].msb == (uint8_t)false) 
        {
            next_hop.dst_port = TBL24_entry[addr_24].next_hop;
        }
        
        //set mac address of the interface to next_hop
        memcpy(&next_hop.dst_mac,&MAC[next_hop.dst_port],sizeof(struct ether_addr));

        return &next_hop;
    }
    return NULL;
}


//allocates memory for routing table, sorts new_entry based on prefix length then inserts entries to routing table

void build_routing_table()
{

    new_entry_t temp_entry;
    unsigned int x,y;

    //Sort the entries based on prefix length.
    for(x = 0;x < (unsigned int)new_entry_nr-1;x++)
    {
        for(y = 0; y < (unsigned int)new_entry_nr-(x+1);y++)
        {
            if(new_entry[y].prefix > new_entry[y+1].prefix)
            {
                memcpy(&temp_entry,&new_entry[y],sizeof(new_entry_t));
                memcpy(&new_entry[y],&new_entry[y+1],sizeof(new_entry_t));
                memcpy(&new_entry[y+1],&temp_entry,sizeof(new_entry_t));
            }
        }
    }
    /* 
      /root/test_frame/framework/test/test.cc:45: Failure
      Expected: __null
      Which is: NULL

      workaround:
      memset:
      fills  the  first  sizeTBL24 * sizeof(TBL24_entry_t)  bytes of the memory area
       pointed to by TBL24_entry with integer -1

    */ 
    memset(&TBL24_entry,-1,sizeTBL24 * sizeof(TBL24_entry_t)); //test fails otherwise

    unsigned int addr;
    uint8_t TBLlong_nr = 0;

    for(x = 0;x <= (unsigned int)new_entry_nr-1;x++)
    {
        puts("New entry:");
        printf("IP-Address: %08d/%d, Port: %d\n",new_entry[x].ip_addr,new_entry[x].prefix,new_entry[x].port);
	//check if prefix < 24
        if(new_entry[x].prefix < 24)
        {
            puts("Length of Prefix less 24 --> TBL24.\n");

            addr = ((0xffffffff << new_entry[x].prefix) & new_entry[x].ip_addr) >> 8;


            //count number of entries
            for(y = 0;y < (unsigned int)(1 << (24-new_entry[x].prefix));y++)
            {
		TBL24_entry[addr+y].next_hop = new_entry[x].port;
                TBL24_entry[addr+y].msb = (uint8_t)false;
            }
        }
	//prefix bigger or equal 24
        else
        {
            puts("Length of Prefix greater or equal 24 --> TBL24 + TBLlong.\n");
            
	    //if TBLlong already contains less specific entry
            if(TBL24_entry[new_entry[x].ip_addr >> 8].msb == (uint8_t)true)
            {
                //get index of TBLlong for this prefix.
                addr = ((TBL24_entry[(new_entry[x].ip_addr) >> 8].next_hop << 8) + (0xff & new_entry[x].ip_addr));
            }
	    //no entry in TBLlong--> add new entry
            else
            {
		//set MSB in TBL24 to 1 --> ask TBLlong
                TBL24_entry[(new_entry[x].ip_addr) >> 8].msb = (uint8_t)true;
                TBL24_entry[(new_entry[x].ip_addr) >> 8].next_hop = TBLlong_nr;

                //get index of TBLlong for this prefix.
                addr = (new_entry[x].ip_addr & 0xff) + (TBLlong_nr << 8);
                ++TBLlong_nr;
            }

	    //Fill the new TBLlong.
            for(y = 0; y < (unsigned int)(1 << (32 - new_entry[x].prefix));y++)
            {
                TBLlong[addr+y] = new_entry[x].port;
            }
        }
    }
}

