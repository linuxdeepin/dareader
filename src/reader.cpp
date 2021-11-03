#include "dareader/reader.h"

#include <unistd.h>

#include <iostream>
#include <thread>

const uint32_t    message_type_frame = 0;
const DA_img_type DA_img_type_RGB24  = 0;
const DA_img_type DA_img_type_GRAY8  = 1;
const DA_img_type DA_img_type_BITMAP = 2;
const DA_img_type DA_img_type_JPEG   = 3;

void do_read_frames(int fd, void * const context, DA_read_frames_cb cb);
bool read_with_retry(int fd, char *buf, size_t len) {
  while (true) {
    errno     = 0;
    ssize_t n = read(fd, buf, len);
    if (n == 0) return false;
    if (n < 0) {
      if (errno == EAGAIN) {
        usleep(1);
        continue;
      } else {
        return false;
      }
    }
    if (n == len) break;
    if (n != len) {
      buf += n;
      len -= n;
    }
  }
  return true;
}

void DA_read_frames(int fd, void * const context, DA_read_frames_cb cb) {
  std::thread t(do_read_frames, fd, context, cb);
  t.detach();
}

class img_reader {
    int                     fd;
    void * const            context;
    DA_read_frames_cb const cb;
    DA_img_type             type;
  public:
    img_reader(int fd, void * const context, DA_read_frames_cb cb) :
      fd(fd), context(context), cb(cb) {
      if (cb == nullptr) {
        std::cerr << "empty cb" << std::endl;
        throw std::string("empty cb");
      }
      char *buf = (char *)malloc(8);
      if (! read_with_retry(fd, buf, 8)) {
        std::cerr << "init failed" << std::endl;
        throw std::string("init failed");
      }
      uint32_t *P = (uint32_t *)buf;
      if (P [0] != message_type_frame) {
        std::cerr << "Wrong message type! not frame" << std::endl;
        throw std::string("Wrong message type! not frame");
      }
      type = P [1];
      free(buf);
    }
    void run() {
      /*
       * data from fd:
       * [1]        message_type --\
       * [2]        image_type   ---*------ read during init
       * [3]        depth     \          \
       * [4]        channel   |          |
       * [5]        width     *-> header *- a session
       * [6]        height    |          |
       * [7]        size      /          |
       * [8:8+size] img data  -> body    /
       */
      char * const header_buf = (char *)malloc(20);
      char        *body_buf   = nullptr;

      char *header_ptr = header_buf;
      char *body_ptr   = body_buf;

      size_t body_size = 0, header_size = 20;

      DA_img img {};

      char * const msg = (char *)malloc(1);
      *msg             = ' ';

      while (true) {
        if (write(fd, msg, 1) == 1) {
        } else {
          std::cerr << "ping server error, errno:" << errno;
          break;
        }
        if (read_with_retry(fd, header_buf, header_size)) {
          uint32_t *P = (uint32_t *)header_buf;
          img.depth   = P [0];
          img.channel = P [1];
          img.width   = P [2];
          img.height  = P [3];
          img.size    = P [4];
          img.type    = type;
        } else {
          break;
        }
        if (body_size < img.size) {
          free(body_buf);
          body_buf  = (char *)malloc(img.size);
          body_ptr  = body_buf;
          body_size = img.size;
        }
        if (read_with_retry(fd, body_buf, img.size)) {
          img.data = body_ptr;
          cb(context, &img);
        } else {
          break;
        }
      }
      free(header_buf);
      free(body_buf);
    }
};

void do_read_frames(int fd, void * const context, DA_read_frames_cb cb) {
  img_reader reader(fd, context, cb);
  reader.run();
}
