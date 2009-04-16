
/*
 * Checksum functions
 * Based on RFC 1071, "Computing the Internet Checksum"
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include "common.h"


/** Reads the contents of the given fd, up to len bytes, and stores the
 * checksum in csum. Returns 1 on success, 0 on error. */
int checksum(int fd, size_t len, uint32_t *csum)
{
	uint8_t *map;

	map = (uint8_t *) mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		return 0;

	*csum = checksum_map(map, len);

	munmap(map, len);
	return 1;
}

/** Calculates the checksum of the given buffer, up to count bytes. Returns
 * the checksum. */
uint32_t checksum_map(uint8_t *map, size_t count)
{
	uint32_t sum = 0;

	while( count > 1 )  {
		sum += * (uint16_t *) map++;
		count -= 2;
	}

	if( count > 0 )
		sum += * (uint8_t *) map;

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

