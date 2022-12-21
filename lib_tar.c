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
  tar_header_t header;
  ssize_t read_size;

  lseek(tar_fd, 0, SEEK_SET);  

  while ((read_size = read(tar_fd, &header, sizeof(tar_header_t))) > 0) {
    // If the current header's name field matches the given path, check if the entry is a symlink
    if (strcmp(path, header.name) == 0) {
      if (header.typeflag == SYMTYPE) {
        return 1;  
      } else {
        return 0; 
      }
    }

    // Move file descriptor to the next header
    lseek(tar_fd, TAR_INT(header.size), 1);  
  }
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
int list(int tar_fd, char *path, char **entries, size_t *no_entries)
{
    // Check if the tar archive is valid
    int num_headers = check_archive(tar_fd);
    if (num_headers < 0)
    {
        // Invalid tar archive
        return 0;
    }

    // Check if the given path exists in the tar archive
    if (!exists(tar_fd, path))
    {
        // Path does not exist
        return 0;
    }

    // Check if the given path is a directory
    if (!is_symlink(tar_fd, path))
    {
        // Path is not a directory
        return 0;
    }

    // Initialize variables
    tar_header_t header;
    char header_buf[BLOCK_SIZE];
    char entry_path[BLOCK_SIZE];
    int entry_count = 0;
    size_t path_len = strlen(path);
    int ret;

    // Seek to the start of the tar archive
    lseek(tar_fd, 0, SEEK_SET);

    // Iterate through all the headers in the tar archive
    for (int i = 0; i < num_headers; i++)
    {
        // Read the header
        ret = read(tar_fd, header_buf, BLOCK_SIZE);
        if (ret < 0)
        {
            // Error reading header
            return 0;
        }

        // Parse the header
        memcpy(&header, header_buf, sizeof(tar_header_t));

        // Check if the entry is a directory or a regular file
        if (header.typeflag == DIRTYPE || header.typeflag == REGTYPE)
        {
            // Check if the entry is at the given path
            if (strncmp(path, header.name, path_len) == 0)
            {
                // Construct the entry path
                snprintf(entry_path, BLOCK_SIZE, "%s", header.name);

                // Add the entry to the list
                strncpy(entries[entry_count], entry_path, BLOCK_SIZE);
                entry_count++;
            }
        }

        // Skip the contents of the entry
        lseek(tar_fd, TAR_INT(header.size), SEEK_CUR);

        // Skip padding
        ret = lseek(tar_fd, BLOCK_SIZE - (TAR_INT(header.size) % BLOCK_SIZE), SEEK_CUR);
        if (ret < 0)
        {
            // Error seeking to the next header
            return 0;
        }
    }

    // Set the number of entries listed
    *no_entries = entry_count;

    // Return success
    return 1;
}

ssize_t read_file(int tar_fd, char *path, size_t offset, uint8_t *dest, size_t *len) {
  // Seek to the start of the tar archive
  if (lseek(tar_fd, 0, SEEK_SET) == -1) {
    perror("lseek failed");
    return -1;
  }
  
  // Iterate through the entries in the tar archive
  while (1) {
    // Read the header for the current entry
    struct posix_header header;
    ssize_t byte = read(tar_fd, &header, sizeof(struct posix_header));
    if (byte == 0) {
      // End of tar archive reached
      return -1;
    } else if (byte != sizeof(struct posix_header)) {
      perror("Error reading tar header");
      return -1;
    }

    // Check if the current entry is the one we're looking for
    if (strcmp(header.name, path) == 0) {
      // Check if the entry is a file
      if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE) {
        // Not a file
        return -1;
      }

      // Check if the offset is outside the file length
      if (offset > TAR_INT(header.size)) {
        return -2;
      }

      // Seek to the start of the file data
      if (lseek(tar_fd, offset, SEEK_CUR) == -1) {
        perror("lseek failed");
        return -1;
      }

      // Read the file data into the destination buffer
      *len = read(tar_fd, dest, *len);
      if (*len == -1) {
        perror("Error reading file data");
        return -1;
      }

      // Return the number of remaining bytes left to be read
      long size = strtol(header.size, NULL, 8);
      return size - *len;
    } else {
      // Skip to the next entry
      if (lseek(tar_fd, TAR_INT(header.size), SEEK_CUR) == -1) {
        perror("lseek failed");
        return -1;
      }
    }
  }
}
