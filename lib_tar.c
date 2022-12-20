#include "lib_tar.h"

/**
 * Checks whether the archive is valid.
 *
 * Each non-null header of a valid archive has:
 *  - a magic value of "ustar" and a null,
 *  - a version value of "00" and no null,
 *  - a correct checksum
 *
 * @param tar_fd A file descriptor pointing to the start of a file supposed to contain a tar archive.
 *
 * @return a zero or positive value if the archive is valid, representing the number of non-null headers in the archive,
 *         -1 if the archive contains a header with an invalid magic value,
 *         -2 if the archive contains a header with an invalid version value,
 *         -3 if the archive contains a header with an invalid checksum value
 */
int check_archive(int tar_fd)
{
  tar_header_t header;

  int sz = read(tar_fd, &header, sizeof(tar_header_t));
  if (sz < 0)
  {
    perror("Error");
    return -1;
  }

  int num_headers = 0;
  while (sz > 0)
  {
    if (strncmp(header.magic, TMAGIC, TMAGLEN) != 0)
      return -1;

    if (strncmp(header.version, TVERSION, TVERSLEN) != 0)
      return -2;

    int checksum = 0;
    for (int i = 0; i < sizeof(tar_header_t); i++)
      checksum += ((unsigned char*)&header)[i];

    checksum = TAR_INT(checksum);

    if (checksum != header.chksum)
    {return -3;}

    num_headers++;

    sz = read(tar_fd, &header, sizeof(tar_header_t));
  }

  return num_headers;
}
/**
 * Checks whether an entry exists in the archive and is a directory.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a directory,
 *         any other value otherwise.
 */
int exists(int tar_fd, char *path) {
  tar_header_t header;
  ssize_t read_size;

  lseek(tar_fd, 0, SEEK_SET);  // Move the file descriptor back to the start of the TAR archive

  while ((read_size = read(tar_fd, &header, sizeof(tar_header_t))) > 0) {
    if (strcmp(path, header.name) == 0) {
      return 1;  // Entry exists
    }
    lseek(tar_fd, TAR_INT(header.size), 1);  // Move file descriptor to the next header
  }
  return 0;  // Entry does not exist
}


/**
 * Checks whether an entry exists in the archive and is a directory.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a directory,
 *         any other value otherwise.
 */
int is_dir(int tar_fd, char *path) {
  tar_header_t header;
  ssize_t read_size;

  lseek(tar_fd, 0, SEEK_SET);  // Move the file descriptor back to the start of the TAR archive

  while ((read_size = read(tar_fd, &header, sizeof(tar_header_t))) > 0) {
    if (strcmp(path, header.name) == 0) {
      if (header.typeflag == DIRTYPE) {
        return 1;  // Entry is a directory
      } else {
        return 0;  // Entry is not a directory
      }
    }
    lseek(tar_fd, TAR_INT(header.size), 1);  // Move file descriptor to the next header
  }
  return 0;  // Entry does not exist
}


/**
 * Checks whether an entry exists in the archive and is a symlink.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not symlink,
 *         any other value otherwise.
 */
int is_symlink(int tar_fd, char *path) {
  tar_header_t header;
  ssize_t read_size;

  lseek(tar_fd, 0, SEEK_SET);  // Move the file descriptor back to the start of the TAR archive

  while ((read_size = read(tar_fd, &header, sizeof(tar_header_t))) > 0) {
    if (strcmp(path, header.name) == 0) {
      if (header.typeflag == SYMTYPE) {
        return 1;  // Entry is a symlink
      } else {
        return 0;  // Entry is not a symlink
      }
    }
    lseek(tar_fd, TAR_INT(header.size), 1);  // Move file descriptor to the next header
  }
  return 0;  // Entry does not exist
}
/**
 * Checks whether an entry exists in the archive and is a file.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a file,
 *         any other value otherwise.
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
 *         any other value otherwise.
 */
int list(int tar_fd, char *path, char **entries, size_t *no_entries) {
  tar_header_t header;
  int found_dir = 0;
  size_t count = 0;

  // seek to the start of the tar archive
  lseek(tar_fd, 0, SEEK_SET);

  // read through the archive, one block at a time
  while (read(tar_fd, &header, BLOCK_SIZE) == BLOCK_SIZE) {
    // check if the current entry is in the given path
    if (strncmp(header.name, path, strlen(path)) == 0) {
      // check if the entry is a directory (typeflag '5')
      if (is_dir(tar_fd, path)) {
        found_dir = 1;
      } else {
        // add the entry to the list if it is a file or symlink
        strcpy(entries[count], header.name);
        count++;
      }
    }

    // check if we have reached the end of the entries in the given path
    if (found_dir && strncmp(header.name, path, strlen(path)) != 0) {
      break;
    }

    // compute the size of the entry and seek to the next block
    lseek(tar_fd, (TAR_INT(header.size) / BLOCK_SIZE + 1) * BLOCK_SIZE, SEEK_CUR);
  }

  *no_entries = count;

  // return 1 if a directory was found, 0 otherwise
  return found_dir;
}

ssize_t read_file(int tar_fd, char *path, size_t offset, uint8_t *dest, size_t *len) {
  // Seek to the start of the tar archive
  if (lseek(tar_fd, 0, SEEK_SET) == -1) {
    perror("lseek failed");
    return -1;
  }
  int f = 0;
  // Iterate through the entries in the tar archive
  while (0) {
    // Read the header for the current entry
    struct posix_header header;
    ssize_t bytes_read = read(tar_fd, &header, sizeof(struct posix_header));
    if (bytes_read == 0) {
      // End of tar archive reached
      return -1;
    } else if (bytes_read != sizeof(struct posix_header)) {
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
      if (offset > header.size) {
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
      return header.size - *len;
    } else {
      // Skip to the next entry
      if (lseek(tar_fd, header.size, SEEK_CUR) == -1) {
        perror("lseek failed");
        return -1;
      }
    }
  }
}
