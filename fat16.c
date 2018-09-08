
#include <stdint.h>
#include <xc.h>
#include "string.h"
#include "os.h"
#include "fat16.h"
#include "flash.h"

static uint8_t _get_mbr(uint16_t idx);
static uint8_t _get_fbr(uint16_t idx);
static uint8_t _get_fat(uint16_t idx);
static uint8_t _get_root(uint16_t idx);
static uint16_t _get_time(void);
static uint16_t _get_date(void);
static void _update_modified_time(rootEntry_t *root);
static uint8_t _get_available_root_entry(void);
static uint16_t _get_empty_cluster(uint16_t first_cluster);
static void _write_fat(uint16_t cluster, uint16_t value);
static uint16_t _read_fat(uint16_t cluster);
static void _write_root(uint8_t slot, rootEntry_t *data);
static void _delete_root(uint8_t slot);
static uint16_t _get_first_cluster(uint8_t slot);
static uint32_t _get_file_size(uint8_t slot);
static void _write_file_size(uint8_t file_number, uint32_t new_file_size);
static uint16_t _sector_from_cluster(uint16_t fat_cluster);

uint8_t buffer[512];

static uint16_t _get_time(void)
{
    uint16_t time;
//    uint8_t hours = rtcc_get_hours_decimal();
//    uint8_t minutes = rtcc_get_minutes_decimal();
//    uint8_t seconds = rtcc_get_seconds_decimal();
//    time = ((hours&0b11111) << 11);
//    time |= ((minutes&0b111111) << 5);
//    time |= ((seconds>>1)&0b11111);
    return time;
};

static uint16_t _get_date(void)
{
    uint16_t date;
//    uint8_t year = rtcc_get_year_decimal();
//    uint8_t month = rtcc_get_month_decimal();
//    uint8_t day = rtcc_get_day_decimal();
//    date = (((year+20)&0b1111111) << 9);
//    date |= ((month&0b1111) << 5);
//    date |= (day&0b11111);
    return date;
}

static void _update_modified_time(rootEntry_t *root)
{
    root->modifiedDate = _get_date();
    root->modifiedTime = _get_time();
}

static uint8_t _root_is_available(uint8_t file_number)
{
    uint16_t root_sector;
    uint16_t offset;
    uint8_t first_byte;
    
    //Sanity check
    //Make sure we have a valid file number
    if(file_number>=FBR_ROOT_ENTRIES)
    {
        //Indicate that root entry is not available
        return 0;
    }
    
    //Find position of root entry
    root_sector = (file_number >> 4) + ROOT_FIRST_SECTOR;
    offset = (file_number & 0b1111);
    offset <<= 5;
    
    //Read the first character of the root entry
    flash_partial_read(root_sector, offset, 1, &first_byte);   
    
    //Check the value of the first byte and return accordingly
    if((first_byte==0x00) || (first_byte==0xE5))
    {
        //Root entry is available
        return 1;
    }
    else
    {
        //Root entry is not available
        return 0;
    }
}

static uint8_t _get_available_root_entry(void)
{
    uint8_t file_number;
    for(file_number=0; file_number<FBR_ROOT_ENTRIES; ++file_number)
    {
        if(_root_is_available(file_number))
        {
            return file_number;
        }
    }
    return 0xFF; //Indicating an error, i.e there are no available slots
}

static uint16_t _get_empty_cluster(uint16_t first_cluster)
{
    uint16_t cluster;
    uint16_t fat_sector;
    uint16_t offset;
    uint16_t value;
    
    //Start at 2, first two positions are reserved
    if(first_cluster<2)
    {
        first_cluster = 2;
    }
    
    for(cluster=first_cluster; cluster<(DATA_NUMBER_OF_SECTORS+2); ++cluster)
    {
        fat_sector = cluster>>8; 
        fat_sector += FAT_FIRST_SECTOR;
        offset = cluster & 0xFF;
        offset <<= 1;
        flash_partial_read(fat_sector, offset, 2, (uint8_t*) &value);
        if(value==0x0000)
        {
            return cluster;
        }
    }
    return 0x0000; //Indicating that there are no free clusters
}

uint16_t fat_get_empty_clusters(uint16_t maximum)
{
    uint16_t empty_clusters = 0;
    uint16_t cluster;
    uint16_t fat_sector;
    uint16_t offset;
    uint16_t value;
    
    //Start at 2, first two positions are reserved
    for(cluster=2; cluster<(DATA_NUMBER_OF_SECTORS+2); ++cluster)
    {
        fat_sector = cluster>>8; 
        fat_sector += FAT_FIRST_SECTOR;
        offset = cluster & 0xFF;
        offset <<= 1;
        flash_partial_read(fat_sector, offset, 2, &value);
        if(value==0x0000)
        {
            ++empty_clusters;
            if(empty_clusters==maximum)
            {
                return empty_clusters;
            }
        }
    }
    return empty_clusters;
}

static void _write_fat(uint16_t cluster, uint16_t value)
{
    uint16_t fat_sector;
    uint16_t offset;
    fat_sector = cluster>>8; 
    fat_sector += FAT_FIRST_SECTOR;
    offset = cluster & 0xFF;
    offset <<= 1;
    flash_partial_write(fat_sector, offset, 2, &value);
}

static uint16_t _read_fat(uint16_t cluster)
{
    uint16_t fat_sector;
    uint16_t offset;
    uint16_t value;
    fat_sector = cluster>>8; 
    fat_sector += FAT_FIRST_SECTOR;
    offset = cluster & 0xFF;
    offset <<= 1;
    flash_partial_read(fat_sector, offset, 2, &value);
    return value;
}

static void _write_root(uint8_t slot, rootEntry_t *data)
{
    uint16_t root_sector;
    uint16_t offset;
    root_sector = (slot >> 4) + ROOT_FIRST_SECTOR;
    offset = (slot & 0b1111);
    offset <<= 5;
    flash_partial_write(root_sector, offset, 32, data);
}

static void _delete_root(uint8_t slot)
{
    uint16_t root_sector;
    uint16_t offset;
    uint8_t deleted_value;
    
    root_sector = (slot >> 4) + ROOT_FIRST_SECTOR;
    offset = (slot & 0b1111);
    offset <<= 5;
    deleted_value = 0xE5;
    flash_partial_write(root_sector, offset, 1, &deleted_value);
}

static uint16_t _get_first_cluster(uint8_t slot)
{
    uint16_t sector;
    uint16_t offset;
    uint16_t first_cluster;
    sector = slot >> 4;
    sector += ROOT_FIRST_SECTOR;
    offset = slot & 0b1111;
    offset <<= 5;
    offset += 26;
    flash_partial_read(sector, offset, 2, &first_cluster);
    return first_cluster;
}

static uint32_t _get_file_size(uint8_t slot)
{
    uint16_t sector;
    uint16_t offset;
    uint32_t file_size;
    sector = slot >> 4;
    sector += ROOT_FIRST_SECTOR;
    offset = slot & 0b1111;
    offset <<= 5;
    offset += 28;
//    flash_partial_write(22, 0, 1, &slot);
//    flash_partial_write(22, 1, 2, &sector);
//    flash_partial_write(22, 3, 2, &offset);
    flash_partial_read(sector, offset, 4, &file_size);
    //file_size = 0x12345678;
//    flash_partial_write(22, 5, 4, &file_size);
    return file_size;
}

static void _write_file_size(uint8_t file_number, uint32_t new_file_size)
{
    uint16_t sector;
    uint16_t offset;
    sector = file_number >> 4;
    sector += ROOT_FIRST_SECTOR;
    offset = file_number & 0b1111;
    offset <<= 5;
    offset += 28;
    flash_partial_write(sector, offset, 4, &new_file_size);
}

uint8_t fat_find_file(char *name, char *extension)
{
    uint8_t slot;
    uint16_t sector;
    uint16_t offset;
    uint8_t buffer[11];
    slot = 0;
    for(sector = ROOT_FIRST_SECTOR; sector <= ROOT_LAST_SECTOR; ++sector)
    {
        for(offset = 0; offset<512; offset += 32)
        {
           flash_partial_read(sector, offset, 11, &buffer);
           if(buffer[0]==0x00)
           {
               //There are no valid entries after a 0x00 entry
               //Indicate that there is no such file
               return 0xFF; 
           }
           else if(buffer[0]==0xE5)
           {
               //This file has been deleted
               //But there might be valid entries after this
               //Just do nothing
           }
           else
           {
               //This is a valid entry, check if name matches
               if(strncmp(name, &buffer[0], 8) == 0)
               {
                   //Name matches so check if extension matches as well
                   if(strncmp(extension, &buffer[8], 3) == 0)
                   {
                       //Both name and extension match
                       //We have found the file we're looking for
                       return slot;
                   }
               }
           }
           ++slot;
        }
    }
    return 0xFF; //Indicating an error, i.e file not found  
}

uint32_t fat_get_file_size(uint8_t file_number)
{
    uint16_t sector;
    uint16_t offset;
    uint32_t file_size;
    sector = file_number >> 4;
    sector += ROOT_FIRST_SECTOR;
    offset = file_number;
    offset <<= 5;
    offset &= 0b111111111;
    offset += 28;
    flash_partial_read(sector, offset, 4, &file_size);
    return file_size;
}

uint8_t fat_create_file(char *name, char *extension, uint32_t size)
{
    uint8_t root_slot;
    rootEntry_t root_entry;
    uint16_t cluster;
    uint16_t next_cluster;
    uint16_t number_of_clusters;
    uint16_t cluster_count;
    
    //Check if a file with this name already exists in root
    //Return an error if the file is found
    if(fat_find_file(name, extension) != 0xFF)
    {
        //We found a file with this name and extension
        //Return an error
        return 0xFF;
    }
    
    //Find an available root slot
    //Return an error if no slot is available
    root_slot = _get_available_root_entry();
    if(root_slot == 0xFF)
    {
        //There is no empty slot
        //Return an error
        return 0xFE;
    }
    
    //Check if there's enough space for the desired file size
    number_of_clusters = (size>>9) + 1;
    if(fat_get_empty_clusters(number_of_clusters) != number_of_clusters)
    {
        //There is not enough space on the drive
        //Return an error
        return 0xFD;
    }
    
    //Obtain the first cluster
    cluster = _get_empty_cluster(2);
    
    //Prepare root entry
    memcpy(root_entry.fileName, name, 8);
    memcpy(root_entry.fileExtension, extension, 3);
    root_entry.attributes = 0x00;
    root_entry.reserved1 = 0x00;
    root_entry.secondFractions = 0x00;
    root_entry.createdTime = _get_time();
    root_entry.createdDate = _get_date();
    root_entry.lastAccessDate = root_entry.createdDate;
    root_entry.reserved2 = 0x00;
    root_entry.modifiedTime = root_entry.createdTime;
    root_entry.modifiedDate = root_entry.createdDate;
    root_entry.firstCluster = cluster;
    root_entry.fileSize = size;
    
    //Write root entry
    _write_root(root_slot, &root_entry);
    
    //Reserve first cluster(s)
    //This code works but is inefficient since it reserves clusters one a the time
    //That's probably fine since we rarely if ever need to reserve more than one
    for(cluster_count=1; cluster_count<=number_of_clusters; ++cluster_count)
    {
        
        if(cluster_count==number_of_clusters)
        {
            //This is the last cluster we need to reserve
            _write_fat(cluster, 0xFFFF);
        }
        else
        {
            //There are more clusters to be reserved
            next_cluster = _get_empty_cluster(cluster+1);
            //Reserve this cluster and place a link to the next cluster
            _write_fat(cluster, next_cluster);
            //Prepare for the next loop
            cluster = next_cluster;
        }
    }
    return root_slot;
}

void fat_delete_file(uint8_t file_number)
{
    uint16_t first_cluster;
    uint16_t next_cluster;
    
    //Make sure we have a valid file number
    if(file_number>=FBR_ROOT_ENTRIES)
    {
        //Nothing to be done
        return;
    }
    
    //Check if file is in use
    if(_root_is_available(file_number))
    {
        //Nothing to be done
        return;
    }
    
    //Get number of first cluster, i.e where to start
    first_cluster = _get_first_cluster(file_number);
    //The last cluster of a chain contains a value above 0xFFF0, usually 0xFFFF
    //Loop and delete until we reach that final cluster
    while(1)
    {
        //Read value of current cluster
        //The value points to the next cluster
        next_cluster = _read_fat(first_cluster);
        
        //Mark the current cluster as re-usable
        _write_fat(first_cluster, 0x0000);
        
        //If that cluster contained 0xFFFF (or similar) before, we're done
        if(next_cluster>0xFFF0)
        {
            break;
        }
        //We're not done, start again
        else
        {
            first_cluster = next_cluster;
        }
    }
    
    //Now all the clusters are marked as re-usable
    //But the file entry in the root still exists
    _delete_root(file_number);
}

static uint16_t _sector_from_cluster(uint16_t fat_cluster)
{
    return fat_cluster + DATA_FIRST_SECTOR - 2;
}

uint8_t fat_append_to_file(uint8_t file_number, uint16_t number_of_bytes, uint8_t *data)
{
    rootEntry_t root;
    uint32_t file_size;
    uint16_t fat_cluster;
    uint16_t new_cluster;
    uint32_t position;
    uint16_t offset;
    uint16_t sector;
    uint16_t length;
    
    //Make sure we have a valid file number
    if(file_number>=FBR_ROOT_ENTRIES)
    {
        //Return an error
        return 0xFF;
    }
    
    //Check if file is in use
    if(_root_is_available(file_number))
    {
        //Return an error
        return 0xFE;
    }

    //Collect data from the root entry
    fat_get_file_information(file_number, &root);
    file_size = root.fileSize;
    fat_cluster = root.firstCluster;
    
    //First, find right cluster, sector and offset
    position = 0;
    while((file_size-position)>512)
    {
        fat_cluster = _read_fat(fat_cluster);
        position += 512;
    }
    
    //Calculate offset
    offset = file_size - position;
    
    //Now we know where to start writing data
    position = 0;
    while(position < number_of_bytes)
    {
        //Do we need another cluster first?
        if(offset==512)
        {
            //Need to add a new cluster
            //Find an unused cluster
            new_cluster = _get_empty_cluster(0x0000);
            //Update the last cluster to point to that new cluster
            _write_fat(fat_cluster, new_cluster);
            //Mark the new cluster as used
            _write_fat(new_cluster, 0xFFFF);
            //Reset fat_cluster and offset
            fat_cluster = new_cluster;
            offset = 0;
        }
        
        //Get physical flash sector from logical cluster address
        sector = _sector_from_cluster(fat_cluster);
        
        //How much data can we/should we write to the current cluster?
        length = 512 - offset;
        if(length>number_of_bytes)
        {
            length = number_of_bytes;
        }
        
        //Write that data
        flash_partial_write(sector, offset, length, &data[position]);
        
        //Update position and offset
        position += length;
        offset += length;
    }
    
    //Now all the data has been written
    //But we still need to update the file size and modified date/time in the root entry
    root.fileSize += number_of_bytes;
    _update_modified_time(&root);
    _write_root(file_number, &root);

    //Report success
    return 0x00;
}

void fat_rename_file(uint8_t file_number, char *name, char *extension)
{
    rootEntry_t root;
    uint8_t cntr;
    
    //Obtain a copy of root entry
    fat_get_file_information(file_number, &root);
    
    //Change file name
    for(cntr=0; cntr<8; ++cntr)
    {
        root.fileName[cntr] = name[cntr];
    }
    
    //Change extension
    for(cntr=0; cntr<3; ++cntr)
    {
        root.fileExtension[cntr] = extension[cntr];
    }
    
    _write_root(file_number, &root);
}

uint8_t fat_read_from_file(uint8_t file_number, uint32_t start_byte, uint32_t length, uint8_t *data)
{
    rootEntry_t root;
    uint16_t cluster;
    uint16_t cluster_number;
    
    //Read root entry
    fat_get_file_information(file_number, &root);
    cluster = root.firstCluster;
    cluster_number = 0;
    
    //Check file size
    if(start_byte+length > root.fileSize)
    {
        //Indicate an error
        return 0xFF;
    }
    
    //Now we know where to start, call fast function
    return fat_read_from_file_fast(start_byte, length, data, &cluster, &cluster_number);
}

uint8_t fat_read_from_file_fast(uint32_t start_byte, uint32_t length, uint8_t *data, uint16_t *cluster, uint16_t *cluster_number)
{
    uint32_t position;
    uint16_t offset;
    uint16_t sector;
    uint16_t read_length;
    uint16_t working_cluster;
    
    //First, find right cluster, sector and offset
    position = (*cluster_number);
    position *= 512;
    if(position > start_byte)
    {
        //Indicate an error
        return 0xFF;
    }
    while((start_byte-position) >= 512)
    {
        (*cluster) = _read_fat((*cluster));
        ++(*cluster_number);
        position += 512;
    }
    
    //Only use working cluster from now on
    //Do no longer update pointers *cluster and *cluster_number
    working_cluster = (*cluster);
    
    //Calculate offset
    offset = start_byte - position;

    //Now we know where to start reading data
    position = 0;
    while(position < length)
    {
        //Do we need another cluster first?
        if(offset==512)
        {
            //Need to get the next cluster cluster
            working_cluster  = _read_fat(working_cluster);
            offset = 0;
        }
        
        //Get physical flash sector from logical cluster address
        sector = _sector_from_cluster(working_cluster);
        
        //How much data can we/should we write to the current cluster?
        read_length = 512 - offset; //Maximum we can read from this cluster
        if(read_length > (length-position))
        {
            //Just read all remaining bytes
            read_length = length - position;
        }
        
        //Read that data
        flash_partial_read(sector, offset, read_length, &data[position]);
        
        //Update position and offset
        position += read_length;
        offset += read_length;
    }
    
    //Indicate success
    return 0x00;
}

uint8_t fat_resize_file(uint8_t file_number, uint32_t new_file_size)
{
    rootEntry_t root;
    uint32_t old_file_size;
    uint16_t cluster;
    uint16_t next_cluster;
    uint16_t old_number_of_clusters;
    uint16_t new_number_of_clusters;
    uint16_t cntr;
    
    //Make sure we have a valid file number
    if(file_number>=FBR_ROOT_ENTRIES)
    {
        //Return an error
        return 0xFF;
    }
    
    //Check if file is in use
    if(_root_is_available(file_number))
    {
        //Return an error
        return 0xFE;
    }

    //Collect data from the root entry
    fat_get_file_information(file_number, &root);
    old_file_size = root.fileSize;
    
    if(new_file_size==old_file_size)
    {
        //Nothing to to, indicate success
        return 0x00;
    }
    
    //Get first cluster
    cluster = root.firstCluster;
    if(cluster==0)
    {
        //There is not a single cluster assigned to this file yet
        cluster = _get_empty_cluster(0x0000);
        root.firstCluster = cluster;
    }

    //Calculate number of clusters necessary
    old_number_of_clusters = (old_file_size+511)>>9;
    new_number_of_clusters = (new_file_size+511)>>9;
    
    //old_number_of_clusters = 1;
    //new_number_of_clusters = 11;
    
    //Reserve clusters if necessary
    if(old_number_of_clusters<new_number_of_clusters)
    {
        for(cntr=1; cntr<=new_number_of_clusters; ++cntr)
        {
            next_cluster = _read_fat(cluster);
            
            if(cntr==new_number_of_clusters)
            {
                //This is the last cluster
                _write_fat(cluster, 0xFFFF);
            }
            else
            {
                //This is not the last cluster
                if((next_cluster>=0xFFF0) || (next_cluster==0))
                {
                    //We need to find another cluster
                    next_cluster = _get_empty_cluster(cluster+1);
                    //Update current cluster with link to next cluster
                    _write_fat(cluster, next_cluster);
                    //Prepare for the next step
                    cluster = next_cluster;
                }
                else
                {
                    //This entry contains a link to the next cluster.
                    //Nothing to do, just prepare for the next step
                    cluster = next_cluster;
                }
            }
        }
    }
    
    //Free clusters if necessary
    else if(old_number_of_clusters>new_number_of_clusters)
    {
        for(cntr=1; cntr<=old_number_of_clusters; ++cntr)
        {
            next_cluster = _read_fat(cluster);
            
            if(cntr<new_number_of_clusters)
            {
                //Nothing to do, just prepare for the next cluster
                cluster = next_cluster;
            }
            else if(cntr==new_number_of_clusters)
            {
                //This is now the last cluster, mark it accordingly
                _write_fat(cluster, 0xFFFF);
                //Prepare for the next cluster
                cluster = next_cluster;
            }
            else
            {
                //Mark this cluster as free
                _write_fat(cluster, 0x0000);
                //Prepare for the next cluster
                cluster = next_cluster;
            }
        }
    }
    
    //Clusters are freed / reserved
    //Only need to update root entry
    root.fileSize = new_file_size;
    _write_root(file_number, &root);
    
    //Indicate success
    return 0x00;
}

void fat_modify_file(uint8_t file_number, uint32_t start_byte, uint16_t length, uint8_t *data)
{
    rootEntry_t root;
    uint16_t cluster;
    uint32_t position;
    uint16_t offset;
    uint16_t sector;
    uint16_t number_of_bytes;
    
    //Read root entry
    fat_get_file_information(file_number, &root);
    cluster = root.firstCluster;
    
    //Make sure we do not write past file end
    if(start_byte>root.fileSize)
    {
        //User wants to write at a position beyond end of file
        return;
    }
    
    if((start_byte+length) > root.fileSize)
    {
        //User wants to write past end of file. Only write until end of file.
        length = root.fileSize - start_byte;
    }
    
    //Find first relevant cluster
    position = 0;
    while((start_byte-position) >= 512)
    {
        cluster = _read_fat(cluster);
        position += 512;
    }
    
    //Calculate offset
    offset = (uint16_t) (start_byte-position);
    
    //Now we know where to start writing data
    position = 0;
    while(position < length)
    {
        //Do we need another cluster first?
        if(offset==512)
        {
            //Find the next cluster
            cluster = _read_fat(cluster);
            //Reset offset
            offset = 0;
        }
        
        //Get physical flash sector from logical cluster address
        sector = _sector_from_cluster(cluster);
        
        //How much data can we/should we write to the current cluster?
        number_of_bytes = 512 - offset;
        if(number_of_bytes > (length-position))
        {
            number_of_bytes = length - position;
        }
        
        //Write that data
        flash_partial_write(sector, offset, number_of_bytes, &data[position]);
        
        //Update position and offset
        position += number_of_bytes;
        offset += number_of_bytes;
    }
}


static uint8_t _get_mbr(uint16_t idx)
{
	switch (idx)
	{
		case 0x1BE:
			return MRB_PARTITION_STATUS;
		case 0x1BF:
            return MBR_PARTITION_START_HEAD;
		case 0x1C0:
            return MBR_PARTITION_START_SECTOR;
		case 0x1C1:
            return MBR_PARTITION_START_CYLINDER;
		case 0x1C2:
			return MBR_PARTITION_TYPE;
		case 0x1C3:
            return MBR_PARTITION_END_HEAD;
		case 0x1C4:
            return MBR_PARTITION_END_SECTOR;
		case 0x1C5:
			return MBR_PARTITION_END_CYLINDER;
		case 0x1C6:
            return LOW_BYTE(LOW_WORD(((uint32_t)MBR_FIRST_PARTITION_SECTOR)));
		case 0x1C7:
			return HIGH_BYTE(LOW_WORD(((uint32_t)MBR_FIRST_PARTITION_SECTOR)));
		case 0x1C8:
			return LOW_BYTE(HIGH_WORD(((uint32_t)MBR_FIRST_PARTITION_SECTOR)));
		case 0x1C9:
            return HIGH_BYTE(HIGH_WORD(((uint32_t)MBR_FIRST_PARTITION_SECTOR)));
		case 0x1CA:
			return LOW_BYTE(LOW_WORD(((uint32_t)MBR_PARTITION_SIZE)));
		case 0x1CB:
			return HIGH_BYTE(LOW_WORD(((uint32_t)MBR_PARTITION_SIZE)));
		case 0x1CC:
			return LOW_BYTE(HIGH_WORD(((uint32_t)MBR_PARTITION_SIZE)));
		case 0x1CD:
			return HIGH_BYTE(HIGH_WORD(((uint32_t)MBR_PARTITION_SIZE)));
		case 0x1FE:
			return HIGH_BYTE((uint16_t)MBR_SIGNATURE);
		case 0x1FF:
			return LOW_BYTE((uint16_t)MBR_SIGNATURE);
		default:
			return 0X00;
	}
}

static uint8_t _get_fbr(uint16_t idx)
{
	switch (idx)
	{
        case 0x00:
            return 0xEB;
        case 0x01:
            return 0x3C;
        case 0x02:
            return 0x90;
		case 0x03:
			return FBR_OEM_IDENTIFIER[0];
		case 0x04:
			return FBR_OEM_IDENTIFIER[1];
		case 0x05:
			return FBR_OEM_IDENTIFIER[2];
		case 0x06:
			return FBR_OEM_IDENTIFIER[3];
		case 0x07:
			return FBR_OEM_IDENTIFIER[4];
		case 0x08:
			return FBR_OEM_IDENTIFIER[5];
		case 0x09:
			return FBR_OEM_IDENTIFIER[6];
		case 0x0A:
			return FBR_OEM_IDENTIFIER[7];
		case 0x0B:
			return LOW_BYTE(((uint16_t)FBR_BYTES_PER_SECTOR));
		case 0x0C:
			return HIGH_BYTE(((uint16_t)FBR_BYTES_PER_SECTOR));
		case 0x0D:
			return FBR_SECTORS_PER_CLUSTER;
		case 0x0E:
            return LOW_BYTE(((uint16_t)FBR_RESERVED_SECTORS));
		case 0x0F:
            return HIGH_BYTE(((uint16_t)FBR_RESERVED_SECTORS));
		case 0x10:
			return FBR_NUMBER_OF_FATS;
		case 0x11:
            return LOW_BYTE(((uint16_t)FBR_ROOT_ENTRIES));
		case 0x12:
            return HIGH_BYTE(((uint16_t)FBR_ROOT_ENTRIES));
		case 0x13:
            return LOW_BYTE(((uint16_t)FBR_NUMBER_OF_SECTORS));
		case 0x14:
            return HIGH_BYTE(((uint16_t)FBR_NUMBER_OF_SECTORS));
		case 0x15:
			return FBR_MEDIA_DESCRIPTOR;
		case 0x16:
            return LOW_BYTE(((uint16_t)FBR_SECTORS_PER_FAT));
		case 0x17:
            return HIGH_BYTE(((uint16_t)FBR_SECTORS_PER_FAT));
		case 0x18:
            return LOW_BYTE(((uint16_t)FBR_SECTORS_PER_HEAD));
		case 0x19:
            return HIGH_BYTE(((uint16_t)FBR_SECTORS_PER_HEAD));
		case 0x1A:
            return LOW_BYTE(((uint16_t)FBR_HEADS_PER_CYLINDER));
		case 0x1B:
            return HIGH_BYTE(((uint16_t)FBR_HEADS_PER_CYLINDER));
		case 0x1C:
            return LOW_BYTE(LOW_WORD(((uint32_t)FBR_HIDDEN_SECTORS)));
		case 0x1D:
            return HIGH_BYTE(LOW_WORD(((uint32_t)FBR_HIDDEN_SECTORS)));
		case 0x1E:
            return LOW_BYTE(HIGH_WORD(((uint32_t)FBR_HIDDEN_SECTORS)));
		case 0x1F:
            return HIGH_BYTE(HIGH_WORD(((uint32_t)FBR_HIDDEN_SECTORS)));
		case 0x28:
            return LOW_BYTE(((uint16_t)FBR_EXT_FLAGS));
		case 0x29:
            return HIGH_BYTE(((uint16_t)FBR_EXT_FLAGS));
		case 0x2C:
            return LOW_BYTE(LOW_WORD(((uint32_t)FBR_ROOT_DIRECTORY_START)));
		case 0x2D:
            return HIGH_BYTE(LOW_WORD(((uint32_t)FBR_ROOT_DIRECTORY_START)));
		case 0x2E:
            LOW_BYTE(HIGH_WORD(((uint32_t)FBR_ROOT_DIRECTORY_START)));
		case 0x2F:
            return HIGH_BYTE(HIGH_WORD(((uint32_t)FBR_ROOT_DIRECTORY_START)));
        case 0x36:
            return 'F';
        case 0x37:
            return 'A';
        case 0x38:
            return 'T';
        case 0x39:
            return '1';
        case 0x3A:
            return '6';
        case 0x3B:
            return ' ';
        case 0x3C:
            return ' ';
        case 0x3D:
            return ' ';
		case 0x1FE:
            return HIGH_BYTE(((uint16_t)FBR_SIGNATURE));
		case 0x1FF:
            return LOW_BYTE(((uint16_t)FBR_SIGNATURE));
		default:
			return 0X00;
	}
}

static uint8_t _get_fat(uint16_t idx)
{
	switch (idx)
	{
        //Bytes 0-4 are reserved
		case 0x00:
			return 0xF8;
		case 0x01:
			return 0xFF;
		case 0x02:
			return 0xFF;
		case 0x03:
			return 0xFF;
        //Bytes 4 & 5 = 0xFFFF. So there is 1 file occupying a single cluster
		case 0x04:
			return 0xFF;
		case 0x05:
			return 0xFF;
		default:
			return 0X00;
	}
}

static uint8_t _get_root(uint16_t idx)
{
	if (idx < 0x20) // Volume information
	{
		if (idx < 11)
		{
			return ROOT_DRIVE_NAME[idx];
		}
        if(idx==11)
        {
            return 0x08;
        }
	}
	if (idx < 0x40) // Example file
	{
		idx -= 0x20;
		if (idx < 0+8)
		{
			return ROOT_FILE_NAME[idx];
		}
		if (idx < 11)
		{
			return ROOT_FILE_EXTENSION[idx-8];
		}
		switch (idx)
		{
            case 11:
                return 0x20;
            case 0x0D:
                return 0x06;
            case 0x0E:
                return 0x28;
            case 0x0F:
                return 0x78;
            case 0x10:
                return 0xDE;
            case 0x11:
                return 0x38;
            case 0x12:
                return 0x5F;
            case 0x13:
                return 0x4B;
            case 0x14:
                return 0x00;
            case 0x15:
                return 0x00;
            case 0x16:
                return 0x04;
            case 0x17:
                return 0x77;
            case 0x18:
                return 0xDE;
            case 0x19:
                return 0x38;
            case 26:
                return LOW_BYTE(((uint16_t)ROOT_FILE_FIRST_CLUSTER));
            case 27:
                return HIGH_BYTE(((uint16_t)ROOT_FILE_FIRST_CLUSTER));
			case 28:
                return LOW_BYTE(LOW_WORD(((uint32_t)ROOT_FILE_SIZE)));
				//return ROOT_FILE_SIZE & 0XFF;
			case 29:
                return HIGH_BYTE(LOW_WORD(((uint32_t)ROOT_FILE_SIZE)));
				//return (ROOT_FILE_SIZE >> 8) & 0XFF;
			case 30:
                return LOW_BYTE(HIGH_WORD(((uint32_t)ROOT_FILE_SIZE)));
				//return (ROOT_FILE_SIZE >> 16) & 0XFF;
			case 31:
                return HIGH_BYTE(HIGH_WORD(((uint32_t)ROOT_FILE_SIZE)));
				//return (ROOT_FILE_SIZE >> 24) & 0XFF;
			default:
				return 0x00;
		}
	}
	return 0X00;
}

static uint8_t _get_data(uint16_t idx)
{
	if (idx < ROOT_FILE_SIZE) // Dummy file
	{
        return ROOT_FILE_CONTENT[idx];
	}
	return 0X00;
}

formatStatus_t fat_get_format_status(void)
{
    uint16_t cntr;
    
    //Check Master Boot Record (sector 0)
    flash_page_read(0, buffer);
    for(cntr=0; cntr<512; ++cntr)
    {
        if(buffer[cntr] != _get_mbr(cntr))
        {
            return DRIVE_NOT_FORMATED;
        }
    }
    
    //Check First Boot Record (sector 1)
    flash_page_read(1, buffer);
    for(cntr=0; cntr<512; ++cntr)
    {
        if(buffer[cntr] != _get_fbr(cntr))
        {
            return DRIVE_NOT_FORMATED;
        }
    }
    
    //If we get here, the drive is formated
    return DRIVE_FORMATED;
}

void fat_format(void)
{
    uint16_t cntr;
    
    //Write MBR
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = _get_mbr(cntr);
    }
    flash_page_write(MBR_SECTOR, buffer);
    
    //Write FBR
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = _get_fbr(cntr);
    }
    flash_page_write(1, buffer);
    
    //Write first FAT sector
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = _get_fat(cntr);
    }
    flash_page_write(FAT_FIRST_SECTOR, buffer);
    
    //Fill remaining FAT sectors (all zeros)
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = 0x00;
    }
    for(cntr=FAT_FIRST_SECTOR+1; cntr<=FAT_LAST_SECTOR; ++cntr)
    {
        flash_page_write(cntr, buffer);
    }
    
    //Write first root sector
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = _get_root(cntr);
    }
    flash_page_write(ROOT_FIRST_SECTOR, buffer);
    
    //Fill remaining root sectors (all zeros)
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = 0x00;
    }
    for(cntr=ROOT_FIRST_SECTOR+1; cntr<=ROOT_LAST_SECTOR; ++cntr)
    {
        flash_page_write(cntr, buffer);
    }
    
    //Write Data of hello world file
    for(cntr=0; cntr<512; ++cntr)
    {
        buffer[cntr] = _get_data(cntr);
    }
    flash_page_write(DATA_FIRST_SECTOR, buffer);
}

void fat_init(void)
{
    //Format flash if necessary
    if(fat_get_format_status()==DRIVE_NOT_FORMATED)
    {
        fat_format();
    }
}

uint8_t fat_get_file_information(uint8_t file_number, rootEntry_t *data)
{
    uint16_t root_sector;
    uint16_t offset;
    
    if(file_number>=FBR_ROOT_ENTRIES)
    {
        //Error: invalid file_number
        return 0x01;
    }
    if(_root_is_available(file_number))
    {
        //Error: there is no file in that location
        return 0x02;
    }

    root_sector = (file_number >> 4) + ROOT_FIRST_SECTOR;
    offset = (file_number & 0b1111);
    offset <<= 5;
    flash_partial_read(root_sector, offset, 32, data);
    
    //Indicate success
    return 0x00;
}