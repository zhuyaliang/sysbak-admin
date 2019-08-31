/**
 * The main program of partclone 
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * clone/restore partition to a image, device or stdout.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <features.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <mcheck.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>

// SHA1 for torrent info
#include "torrent_helper.h"

/**
 * progress.h - only for progress bar
 */
#include "progress.h"

void *thread_update_pui(void *arg);
/// progress_bar structure defined in progress.h
progress_bar prog;
unsigned long long copied;
unsigned long long block_id;
int done;

#include "partclone.h"

/// cmd_opt structure defined in partclone.h
cmd_opt opt;

#include "checksum.h"

/// fs option
#include "fs_common.h"
/// cmd_opt structure defined in partclone.h
fs_cmd_opt fs_opt;

/**
 * main function - for clone or restore data
 */
int main(int argc, char **argv) {
#ifdef MEMTRACE
	setenv("MALLOC_TRACE", "partclone_mtrace.log", 1);
	mtrace();
#endif
	char*			source;			/// source data
	char*			target;			/// target data
	int			dfr, dfw;		/// file descriptor for source and target
	int			r_size, w_size;		/// read and write size
	unsigned		cs_size = 0;		/// checksum_size
	int			cs_reseed = 1;
	unsigned long *bitmap = NULL;		/// the point for bitmap data
	int			debug = 0;		/// debug level
	int			tui = 0;		/// text user interface
	int			pui = 0;		/// progress mode(default text)
	
	int			flag;
	int			pres = 0;
	pthread_t		prog_thread;
	void			*p_result;
	struct stat st_dev;

	static const char *const bad_sectors_warning_msg =
		"*************************************************************************\n"
		"* WARNING: The disk has bad sectors. This means physical damage on the  *\n"
		"* disk surface caused by deterioration, manufacturing faults, or        *\n"
		"* another reason. The reliability of the disk may remain stable or      *\n"
		"* degrade quickly. Use the --rescue option to efficiently save as much  *\n"
		"* data as possible!                                                     *\n"
		"*************************************************************************\n";

	file_system_info fs_info;   /// description of the file system
	image_options    img_opt;

	parse_options(argc, argv, &opt);
	memset(&fs_opt, 0, sizeof(fs_cmd_opt));
	debug = opt.debug;
	fs_opt.debug = debug;
	fs_opt.ignore_fschk = opt.ignore_fschk;

	pui = TEXT;

	if (opt.dd || opt.domain) 
    {

		log_mesg(1, 0, 0, debug, "Initiate image options - version %s\n", IMAGE_VERSION_CURRENT);
		img_opt.checksum_mode = opt.checksum_mode;
		img_opt.checksum_size = get_checksum_size(opt.checksum_mode, opt.debug);
		img_opt.blocks_per_checksum = opt.blocks_per_checksum;
		img_opt.reseed_checksum = opt.reseed_checksum;
		log_mesg(1, 0, 0, debug, "Initial image hdr - get Super Block from partition\n");
		log_mesg(1, 0, 1, debug, "Reading Super Block\n");

		/// get Super Block information from partition
		read_super_blocks(source, &fs_info);

		check_mem_size(fs_info, img_opt);

		/// alloc a memory to restore bitmap
		bitmap = pc_alloc_bitmap(fs_info.totalblock);
		if (bitmap == NULL) {
			log_mesg(0, 1, 1, debug, "%s, %i, not enough memory\n", __func__, __LINE__);
		}

		log_mesg(2, 0, 0, debug, "initial main bitmap pointer %p\n", bitmap);
		log_mesg(1, 0, 0, debug, "Initial image hdr - read bitmap table\n");

		/// read and check bitmap from partition
		log_mesg(0, 0, 1, debug, "Calculating bitmap... Please wait... ");
		read_bitmap(source, fs_info, bitmap, pui);

		/// check the dest partition size.
		if (opt.dd && opt.check) {
		    if (!opt.restore_raw_file)
			check_size(&dfw, fs_info.device_size);
		    else
			check_free_space(target, fs_info.device_size);
		}

		log_mesg(2, 0, 0, debug, "check main bitmap pointer %p\n", bitmap);
		log_mesg(0, 0, 1, debug, "done!\n");
	}

	log_mesg(1, 0, 0, debug, "print image information\n");

	/// print option to log file
	if (debug)
		print_opt(opt);

	print_file_system_info(fs_info, opt);

	/**
	 * initial progress bar
	 */
	start = 0;				/// start number of progress bar
	stop = (fs_info.usedblocks);		/// get the end of progress number, only used block
	log_mesg(1, 0, 0, debug, "Initial Progress bar\n");
	/// Initial progress bar
	if (opt.no_block_detail)
		flag = NO_BLOCK_DETAIL;
	else
		flag = IO;
	progress_init(&prog, start, stop, fs_info.totalblock, flag, fs_info.block_size);
	copied = 0;				/// initial number is 0

	/**
	 * thread to print progress
	 */
	pres = pthread_create(&prog_thread, NULL, thread_update_pui, NULL);
	if(pres)
	    log_mesg(0, 1, 1, debug, "%s, %i, thread create error\n", __func__, __LINE__);


	/**
	 * start read and write data between source and destination
	 */
	if (opt.clone) {

		const unsigned long long blocks_total = fs_info.totalblock;
		const unsigned int block_size = fs_info.block_size;
		const unsigned int buffer_capacity = opt.buffer_size > block_size ? opt.buffer_size / block_size : 1; // in blocks
		unsigned char checksum[cs_size];
		unsigned int blocks_in_cs, blocks_per_cs, write_size;
		char *read_buffer, *write_buffer;

		// SHA1 for torrent info
		int tinfo = -1;
		torrent_generator torrent;

		blocks_per_cs = img_opt.blocks_per_checksum;

		log_mesg(1, 0, 0, debug, "#\nBuffer capacity = %u, Blocks per cs = %u\n#\n", buffer_capacity, blocks_per_cs);

		write_size = cnv_blocks_to_bytes(0, buffer_capacity, block_size, &img_opt);

		read_buffer = (char*)malloc(buffer_capacity * block_size);
		write_buffer = (char*)malloc(write_size + cs_size);

		if (read_buffer == NULL || write_buffer == NULL) {
			log_mesg(0, 1, 1, debug, "%s, %i, not enough memory\n", __func__, __LINE__);
		}

		/// read data from the first block
		if (lseek(dfr, 0, SEEK_SET) == (off_t)-1)
			log_mesg(0, 1, 1, debug, "source seek ERROR:%s\n", strerror(errno));

		log_mesg(0, 0, 0, debug, "Total block %llu\n", blocks_total);

		/// start clone partition to image file
		log_mesg(1, 0, 0, debug, "start backup data...\n");

		blocks_in_cs = 0;
		init_checksum(img_opt.checksum_mode, checksum, debug);

		if (opt.blockfile == 1) {
			char torrent_name[PATH_MAX + 1] = {'\0'};
			sprintf(torrent_name,"%s/torrent.info", target);
			tinfo = open(torrent_name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

			torrent_init(&torrent, tinfo);
		}

		block_id = 0;
		do {
			/// scan bitmap
			unsigned long long i, blocks_skip, blocks_read;
			unsigned int cs_added = 0, write_offset = 0;
			off_t offset;

			/// skip unused blocks
			for (blocks_skip = 0;
			     block_id + blocks_skip < blocks_total &&
			     !pc_test_bit(block_id + blocks_skip, bitmap, fs_info.totalblock);
			     blocks_skip++);
			if (block_id + blocks_skip == blocks_total)
				break;

			if (blocks_skip)
				block_id += blocks_skip;

			/// read blocks
			for (blocks_read = 0;
			     block_id + blocks_read < blocks_total && blocks_read < buffer_capacity &&
			     pc_test_bit(block_id + blocks_read, bitmap, fs_info.totalblock);
			     ++blocks_read);
			if (!blocks_read)
				break;

			offset = (off_t)(block_id * block_size);
			if (lseek(dfr, offset, SEEK_SET) == (off_t)-1)
				log_mesg(0, 1, 1, debug, "source seek ERROR:%s\n", strerror(errno));

			r_size = read_all(&dfr, read_buffer, blocks_read * block_size);
			if (r_size != (int)(blocks_read * block_size)) {
				if ((r_size == -1) && (errno == EIO)) {
					if (opt.rescue) {
						memset(read_buffer, 0, blocks_read * block_size);
						for (r_size = 0; r_size < blocks_read * block_size; r_size += PART_SECTOR_SIZE)
							rescue_sector(&dfr, offset + r_size, read_buffer + r_size, &opt);
					} else
						log_mesg(0, 1, 1, debug, "%s", bad_sectors_warning_msg);
				} else
					log_mesg(0, 1, 1, debug, "read error: %s\n", strerror(errno));
			}

			log_mesg(2, 0, 0, debug, "blocks_read = %i\n", blocks_read);

			/// calculate checksum
			if (opt.blockfile == 0) {
				for (i = 0; i < blocks_read; ++i) {

					memcpy(write_buffer + write_offset,
						read_buffer + i * block_size, block_size);

					write_offset += block_size;

					update_checksum(checksum, read_buffer + i * block_size, block_size);

					if (blocks_per_cs > 0 && ++blocks_in_cs == blocks_per_cs) {
					    log_mesg(3, 0, 0, debug, "CRC = %x%x%x%x \n", checksum[0], checksum[1], checksum[2], checksum[3]);

						memcpy(write_buffer + write_offset, checksum, cs_size);

						++cs_added;
						write_offset += cs_size;

						blocks_in_cs = 0;
						if (cs_reseed)
							init_checksum(img_opt.checksum_mode, checksum, debug);
					}
				}
			}

			/// write buffer to target
			if (opt.blockfile == 1) {
				// SHA1 for torrent info
				// Not always bigger or smaller than 16MB

				// first we write out block_id * block_size for filename
				// because when calling write_block_file
				// we will create a new file to describe a continuous block (or buffer is full)
				// and never write to same file again
				torrent_start_offset(&torrent, block_id * block_size);
				torrent_end_length(&torrent, blocks_read * block_size);

				torrent_update(&torrent, read_buffer, blocks_read * block_size);

				if (opt.torrent_only == 1) {
					w_size = blocks_read * block_size;
				} else {
					w_size = write_block_file(target, read_buffer, blocks_read * block_size, block_id * block_size);
				}
			} else {
				w_size = write_all(&dfw, write_buffer, write_offset);
				if (w_size != write_offset)
					log_mesg(0, 1, 1, debug, "image write ERROR:%s\n", strerror(errno));
			}

			/// count copied block
			copied += blocks_read;
			log_mesg(2, 0, 0, debug, "copied = %lld\n", copied);

			/// next block
			block_id += blocks_read;

			/// read or write error
			if (r_size + cs_added * cs_size != w_size)
				log_mesg(0, 1, 1, debug, "read(%i) and write(%i) different\n", r_size, w_size);

		} while (1);

		if (opt.blockfile == 1) {
			torrent_final(&torrent);
		} else {
			if (blocks_in_cs > 0) {

				// Write the checksum for the latest blocks
				log_mesg(1, 0, 0, debug, "Write the checksum for the latest blocks. size = %i\n", cs_size);
				log_mesg(3, 0, 0, debug, "CRC = %x%x%x%x \n", checksum[0], checksum[1], checksum[2], checksum[3]);
				w_size = write_all(&dfw, (char*)checksum, cs_size);
				if (w_size != cs_size)
					log_mesg(0, 1, 1, debug, "image write ERROR:%s\n", strerror(errno));
			}
		}

		free(write_buffer);
		free(read_buffer);

	// check only the size when the image does not contains checksums and does not
	// comes from a pipe
	} 
	else if (opt.dd) {

		char *buffer;
		int block_size = fs_info.block_size;
		unsigned long long blocks_total = fs_info.totalblock;
		int buffer_capacity = block_size < opt.buffer_size ? opt.buffer_size / block_size : 1;

		buffer = (char*)malloc(buffer_capacity * block_size);
		if (buffer == NULL) {
			log_mesg(0, 1, 1, debug, "%s, %i, not enough memory\n", __func__, __LINE__);
		}

		block_id = 0;

		if (lseek(dfr, 0, SEEK_SET) == (off_t)-1)
			log_mesg(0, 1, 1, debug, "source seek ERROR:%d\n", strerror(errno));

		log_mesg(0, 0, 0, debug, "Total block %llu\n", blocks_total);

		/// start clone partition to partition
		log_mesg(1, 0, 0, debug, "start backup data device-to-device...\n");
		do {
			/// scan bitmap
			unsigned long long blocks_skip, blocks_read;
			off_t offset;

			/// skip unused blocks
			for (blocks_skip = 0;
			     block_id + blocks_skip < blocks_total &&
			     !pc_test_bit(block_id + blocks_skip, bitmap, fs_info.totalblock);
			     blocks_skip++);

			if (block_id + blocks_skip == blocks_total)
				break;

			if (blocks_skip)
				block_id += blocks_skip;

			/// read chunk from source
			for (blocks_read = 0;
			     block_id + blocks_read < blocks_total && blocks_read < buffer_capacity &&
			     pc_test_bit(block_id + blocks_read, bitmap, fs_info.totalblock);
			     ++blocks_read);

			if (!blocks_read)
				break;

			offset = (off_t)(block_id * block_size);
			if (lseek(dfr, offset, SEEK_SET) == (off_t)-1)
				log_mesg(0, 1, 1, debug, "source seek ERROR:%s\n", strerror(errno));
			if (lseek(dfw, offset + opt.offset, SEEK_SET) == (off_t)-1)
				log_mesg(0, 1, 1, debug, "target seek ERROR:%s\n", strerror(errno));

			r_size = read_all(&dfr, buffer, blocks_read * block_size);
			if (r_size != (int)(blocks_read * block_size)) {
				if ((r_size == -1) && (errno == EIO)) {
					if (opt.rescue) {
						memset(buffer, 0, blocks_read * block_size);
						for (r_size = 0; r_size < blocks_read * block_size; r_size += PART_SECTOR_SIZE)
							rescue_sector(&dfr, offset + r_size, buffer + r_size, &opt);
					} else
						log_mesg(0, 1, 1, debug, "%s", bad_sectors_warning_msg);
				} else
					log_mesg(0, 1, 1, debug, "source read ERROR %s\n", strerror(errno));
			}

			/// write buffer to target
			w_size = write_all(&dfw, buffer, blocks_read * block_size);
			if (w_size != (int)(blocks_read * block_size)) {
				if (opt.skip_write_error)
					log_mesg(0, 0, 1, debug, "skip write block %lli error:%s\n", block_id, strerror(errno));
				else
					log_mesg(0, 1, 1, debug, "write block %lli ERROR:%s\n", block_id, strerror(errno));
			}

			/// count copied block
			copied += blocks_read;

			/// next block
			block_id += blocks_read;

			/// read or write error
			if (r_size != w_size) {
				if (opt.skip_write_error)
					log_mesg(0, 0, 1, debug, "read and write different\n");
				else
					log_mesg(0, 1, 1, debug, "read and write different\n");
			}
		} while (1);

		free(buffer);

		/// restore_raw_file option
		if (opt.restore_raw_file && !pc_test_bit(blocks_total - 1, bitmap, fs_info.totalblock)) {
		    if (ftruncate(dfw, (off_t)fs_info.device_size) == -1){
			log_mesg(0, 0, 1, debug, "ftruncate ERROR:%s\n", strerror(errno));
		    }
		    log_mesg(1, 0, 0, debug, "ftruncate:%llu\n", (off_t)fs_info.device_size);
		}

	} 

	done = 1;
	pres = pthread_join(prog_thread, &p_result);
	if(pres)
	    log_mesg(0, 1, 1, debug, "%s, %i, thread join error\n", __func__, __LINE__);
	update_pui(&prog, copied, block_id, done);
#ifndef CHKIMG
	sync_data(dfw, &opt);
#endif
	print_finish_info(opt);

	/// close source
	close(dfr);
	/// close target
	if (dfw != -1)
		close_target(dfw);
	/// free bitmp
	free(bitmap);
	close_pui(pui);
#ifndef CHKIMG
	fprintf(stderr, "Cloned successfully.\n");
#else
	printf("Checked successfully.\n");
#endif
	if (opt.debug)
		close_log();
#ifdef MEMTRACE
	muntrace();
#endif
	return 0;      /// finish
}

void *thread_update_pui(void *arg) {

	pthread_exit("exit");
}
