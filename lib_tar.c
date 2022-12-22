#include "lib_tar.h"

/**
 * Checks whether the archive is valid.
 *
 * Each non-null header of a valid archive has:
 *  - a magic size of "ustar" and a null,
 *  - a version size of "00" and no null,
 *  - a correct checksum
 *
 * @param tar_fd A file descriptor pointing to the start of a file supposed to contain a tar archive.
 *
 * @return a zero or positive size if the archive is valid, representing the number of non-null headers in the archive,
 *         -1 if the archive contains a header with an invalid magic size,
 *         -2 if the archive contains a header with an invalid version size,
 *         -3 if the archive contains a header with an invalid checksum size
 */

int check_chksum(tar_header_t header) {
  int i;
  int checksum = 0;

  for (i = 0; i < 512; i++) {
    char c = ((char*)&header)[i];
    if (i < 148 || i > 155) {
      // On prend en compte tous les octets de l'en-tête sauf les 8 octets
      // du champ checksum
      checksum += (unsigned char)c;
    } else {
      // On met à zéro les 8 octets du champ checksum
      checksum += ' ';
    }
  }

  // On convertit le checksum en octal et on le compare au checksum stocké
  // dans l'en-tête
  int stored_chksum = TAR_INT(header.chksum);
  return (checksum == stored_chksum);
}

int check_archive(int tar_fd) {
  tar_header_t header;

  int size = read(tar_fd, &header, sizeof(tar_header_t));
  if (size < 0)
  {
    perror("Error");
    return -1;
  }

  int num_headers = 0;
  while (size > 0) {
    if (strncmp(header.magic, TMAGIC, TMAGLEN) != 0) return -1;
    if (strncmp(header.version, TVERSION, TVERSLEN) != 0) return -2;
    if (!check_chksum(header)) return -3;
    num_headers++;
    size = read(tar_fd, &header, sizeof(tar_header_t));
  }
  if (size == 0) return num_headers;

  // Return -1 if an error occurred while reading the tar archive
  return -1;
}


/**
 * Checks whether an entry exists in the archive and is a directory.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a directory,
 *         any other size otherwise.
 */

int exists(int tar_fd, char *path) {
    tar_header_t header;
    // Read through the tar archive, one header at a time.
    while (read(tar_fd, &header, sizeof(header)) > 0) {
        // If the current header's name field matches the given path, return 1.
        if (strcmp(header.name, path) == 0) {
            return 1;
        }
        if (header.typeflag == REGTYPE || header.typeflag == AREGTYPE) {
            off_t file_size = TAR_INT(header.size);
            // Compute the number of padding blocks.
            off_t blocks = (BLOCK_SIZE - (file_size % BLOCK_SIZE)) % BLOCK_SIZE;
            // Seek past the data blocks and the padding blocks.
            lseek(tar_fd, file_size + blocks, SEEK_CUR);
        }
    }
    return 0;
}


/**
 * Checks whether an entry exists in the archive and is a directory.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a directory,
 *         any other size otherwise.
 */
int is_dir(int tar_fd, char *path) {
  // Check if the entry exists in the archive
  if (!exists(tar_fd, path)) {
    return 0;
  }

  // Reset file pointer to start of archive
  lseek(tar_fd, 0, 0);

  tar_header_t header;
  while (1) {
    // Read in the header
    size_t bytes_read = read(tar_fd, &header, sizeof(tar_header_t));
    if (bytes_read == 0) {
      // End of file reached, entry not found
      return 0;
    } else if (bytes_read < 0) {
      // Error reading from file
      return 0;
    }

    // Check if the header is null (all fields are set to null characters)
    int null_header = 1;
    for (size_t i = 0; i < sizeof(tar_header_t); i++) {
      if (((char*)&header)[i] != '\0') {
        null_header = 0;
        break;
      }
    }
    if (null_header) {
      // Null header found, skip over it and continue
      continue;
    }

    // Check if the header's path matches the given path and the typeflag is set to '5' for a directory
    if (strcmp(header.name, path) == 0 && header.typeflag == '5') {
      // Entry found and it is a directory
      return 1;
	}
	// Skip over the file data
	off_t file_size = TAR_INT(header.size);
	off_t padding_size = (file_size % BLOCK_SIZE == 0) ? 0 : BLOCK_SIZE - (file_size % BLOCK_SIZE);
	lseek(tar_fd, file_size + padding_size, 1);
     }
}

/**
 * Checks whether an entry exists in the archive and is a symlink.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not symlink,
 *         any other size otherwise.
 */
int is_symlink(int tar_fd, char *path) {
  // Check if the entry exists in the tar archive
  if (exists(tar_fd, path) == 0) {
    return 0;
  }

  // Read the tar header for the entry
  tar_header_t header;
  lseek(tar_fd, 0, SEEK_SET);
  while (read(tar_fd, &header, BLOCK_SIZE) > 0) {
    // Check if the name field of the header matches the given path
    if (strcmp(header.name, path) == 0) {
      // Check if the typeflag field is set to 'L'
      if (header.typeflag == SYMTYPE) {
        return 1;
      } else {
        return 0;
      }
    }

    // Calculate the size of the entry in blocks
    int size = TAR_INT(header.size);
    int blocks = (size / BLOCK_SIZE) + ((size % BLOCK_SIZE) ? 1 : 0);

    // Seek to the next entry in the tar archive
    lseek(tar_fd, blocks * BLOCK_SIZE, SEEK_CUR);
  }

  // Return 0 if the entry was not found
  return 0;
}

/**
 * Checks whether an entry exists in the archive and is a file.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a file,
 *         any other size otherwise.
 */
 int is_file(int tar_fd, char *path) {
  tar_header_t header;

  // seek to the start of the tar archive
  lseek(tar_fd, 0, SEEK_SET);

  // read through the archive, one block at a time
  while (read(tar_fd, &header, BLOCK_SIZE) == BLOCK_SIZE) {
    // check if the current entry is the one we're looking for
    if (strcmp(header.name, path) == 0) {
      // check if the entry is a file (typeflag '0' or '\0')
      if (header.typeflag == '0' || header.typeflag == '\0') {
        return 1;
      } else {
        return 0;
      }
    }

    lseek(tar_fd, TAR_INT(header.size), 1);
  }

  // if we reach this point, it means the entry was not found
  return 0;
}

/**
 * Lists the entries at a given path in the archive.
 * list() does not recurse into the directories listed at the given path.
 *
 * Example:
 *  dir/          list(..., "dir/", ...) lists "dir/a", "dir/b", "dir/c/" and "dir/e/"
 *   ├── a
 *   ├── b
 *   ├── c/
 *   │   └── d
 *   └── e/
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive. If the entry is a symlink, it must be resolved to its linked-to entry.
 * @param entries An array of char arrays, each one is long enough to contain a tar entry path.
 * @param no_entries An in-out argument.
 *                   The caller set it to the number of entries in `entries`.
 *                   The callee set it to the number of entries listed.
 *
 * @return zero if no directory at the given path exists in the archive,
 *         any other size otherwise.
 */
int list(int tar_fd, char *path, char **entries, size_t *no_entries) {
  int entry_count = 0;
  if (!exists(tar_fd, path)) {
    return 0;
  }
  // Read the tar headers until we reach the end of the archive.
  while (*no_entries > 0) {
    // Read a tar header.
    char header[512];
    ssize_t bytes_read = read(tar_fd, header, 512);
    if (bytes_read == 0) {
      // End of archive reached.
      break;
    }
    if (bytes_read < 0 || (size_t)bytes_read < 512) {
      // Invalid header.
      return -1;
    }

    // Check if the entry is in the given path.
    if (strncmp(header, path, strlen(path)) == 0) {
      // Check if the entry is a directory or a regular file, or a symlink.
      if (header[156] == '5') {
        // The entry is a directory.
        // Add it to the entries array.
	entries[entry_count] = malloc(100);
	if (entries[entry_count] == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
      }
        strcpy(entries[entry_count++], header);
        (*no_entries)--;
      } else if (header[156] == '0') {
        // The entry is a regular file.
        // Add it to the entries array.
	entries[entry_count] = malloc(100);
	if (entries[entry_count] == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
        strcpy(entries[entry_count++], header);
        (*no_entries)--;
      } else if (header[156] == '2') {
        // The entry is a symlink.
        // Resolve the symlink by updating the path to its linked-to entry.
        path = &header[157];
      }
    }

    // Skip to the next header by seeking to the correct position in the file.
    int file_size = TAR_INT(&header[124]);
    if (lseek(tar_fd, file_size + (file_size % 512), SEEK_CUR) < 0) {
      return -1;
    }
  }

  // Update the number of listed entries.
  *no_entries = entry_count;

  return entry_count > 0 ? entry_count : -1;
}

size_t read_file(int tar_fd, char *path, size_t offset, uint8_t *dest, size_t *len) {
  tar_header_t header;
  while (read(tar_fd, &header, BLOCK_SIZE) > 0) {
    // Check if the entry is the file we are looking for.
    if (strcmp(header.name, path) == 0) {
      // Check if the entry is a regular file.
      if (! is_file(tar_fd, path) && ! is_symlink(tar_fd, path)) {
        return -1;
      }

      // Check if the entry is a symlink, and resolve it if necessary.
      if (header.typeflag == SYMTYPE) {
        path = header.linkname;
        continue;
      }

      // Check if the offset is within the file bounds.
      int file_size = TAR_INT(header.size);
      if (offset > file_size) {
        return -2;
      }

      // Seek to the correct position in the file.
      if (lseek(tar_fd, offset, SEEK_CUR) < 0) {
        return -1;
      }

      // Read the file into the destination buffer.
      ssize_t bytes_read = read(tar_fd, dest, *len);
      if (bytes_read < 0) {
        return -1;
      }

      *len = bytes_read;

      // Check if we have read the entire file.
      if (bytes_read < file_size - offset) {
        return file_size - offset - bytes_read;
      } else {
        return 0;
      }
    } else {
      // Skip to the next header by seeking to the correct position in the file.
      int file_size = TAR_INT(header.size);
      if (lseek(tar_fd, file_size + (file_size % BLOCK_SIZE), SEEK_CUR) < 0) {
        return -1;
      }
    }
  }

  // File not found in the archive.
  return -1;
}

