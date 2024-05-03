int set(int physical_address)
{

}
#define MSHR_ENTRY 16
#define RQ_SIZE 12

class Queue_entry{
    public:
        int PAaddress;
        int fill_level;
    
};
class Queue_Data{
    public:
        int occupancy;
        int front;
        int rear;
        int signature;
        Queue_entry* entry;

        Queue_Data()
        {
            occupancy=0;
            front=0;
            rear=0;
            signature=0;
        }

};
class MSHR{
    public:
        int tag;
        int PAaddress;
        int isServiced;
        int entry_count;
};

MSHR mshr[MSHR_ENTRY];
Queue_Data Read_Queue[RQ_SIZE];

void insert_into_MSHR(int Address)
{

    for(int i=0;i<MSHR_ENTRY;i++)
    {
        //if(mshr->tag==ad)
    }
    if(mshr->entry_count<MSHR_ENTRY)
    {
        mshr->tag=tag(Address);
        mshr->isServiced=0;
        mshr->entry_count++;

        if(Read_Queue->occupancy<RQ_SIZE)
        {
            Insert_Into_Read_Queue(mshr->tag,mshr->PAaddress);
        }
       
 Insert_Into_Read_Queue(mshr->tag,mshr->PAaddress);
        //get the read queue oldest entry





    }

    
}

int Replacement_Victim(int tag,int PAaddress)
{

}



void Insert_Into_Read_Queue(int tag,int PAaddress)
{
    Read_Queue[Read_Queue->rear].entry->PAaddress=PAaddress;
    Read_Queue->rear++;
   //int way= Replacement_Victim(tag, PAaddress);


}


void Remove_from_Queue()
{

}