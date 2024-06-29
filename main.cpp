// MIT License
// Copyright (c) [2024] [Oren Collaco]
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <png.h>
#include <stdint.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
    void   *start;
    size_t length;
};

static void process_image(const void *p, int size, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error opening output file");
        exit(EXIT_FAILURE);
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    png_init_io(png, fp);

    // Assuming 1280x720 resolution, adjust as needed
    int width = 1280;
    int height = 720;

    png_set_IHDR(png, info, width, height, 16, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    png_bytep row = static_cast<png_bytep>(malloc(3 * width * sizeof(uint16_t)));

    // Convert RG10 to RGB16
    for (int y = 0; y < height; y++) {
        const uint16_t *src = static_cast<const uint16_t*>(p) + y * width;
        uint16_t *dst = reinterpret_cast<uint16_t*>(row);
        for (int x = 0; x < width; x++) {
            uint16_t r = src[x] & 0x03FF;
            uint16_t g = (src[x] >> 10) & 0x03FF;
            uint16_t b = src[x + 1] & 0x03FF;
            
            dst[3 * x] = r << 6;
            dst[3 * x + 1] = g << 6;
            dst[3 * x + 2] = b << 6;
        }
        png_write_row(png, row);
    }

    free(row);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}

int main() {
    struct v4l2_format              fmt;
    struct v4l2_buffer              buf;
    struct v4l2_requestbuffers      req;
    enum v4l2_buf_type              type;
    fd_set                          fds;
    struct timeval                  tv;
    int                             r, fd = -1;
    unsigned int                    i, n_buffers;
    const char                      *dev_name = "/dev/video0";
    char                            out_name[256];
    struct buffer                   *buffers;

    printf("Opening device: %s\n", dev_name);
    fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("Cannot open device");
        exit(EXIT_FAILURE);
    }
    printf("Device opened successfully\n");

    // Check if the device supports video capture
    v4l2_capability cap;
    if (-1 == ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        perror("VIDIOC_QUERYCAP");
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "The device does not support video capture\n");
        exit(EXIT_FAILURE);
    }

    printf("Device capabilities: %08x\n", cap.capabilities);

    // Set format
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = 1280;
    fmt.fmt.pix.height      = 720;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SRGGB10;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    printf("Setting format...\n");
    if (-1 == ioctl(fd, VIDIOC_S_FMT, &fmt)) {
        perror("VIDIOC_S_FMT");
        exit(EXIT_FAILURE);
    }

    // Query the set format
    if (-1 == ioctl(fd, VIDIOC_G_FMT, &fmt)) {
        perror("VIDIOC_G_FMT");
        exit(EXIT_FAILURE);
    }

    printf("Format set:\n");
    printf("  Width: %d\n", fmt.fmt.pix.width);
    printf("  Height: %d\n", fmt.fmt.pix.height);
    printf("  Pixel format: %c%c%c%c\n",
           fmt.fmt.pix.pixelformat & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    // Set frame interval (try to set 30 fps)
    struct v4l2_streamparm streamparm;
    CLEAR(streamparm);
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = 30;

    if (-1 == ioctl(fd, VIDIOC_S_PARM, &streamparm)) {
        perror("VIDIOC_S_PARM");
    } else {
        printf("Frame interval set to %d/%d\n", 
               streamparm.parm.capture.timeperframe.numerator,
               streamparm.parm.capture.timeperframe.denominator);
    }

    // Try to set some camera-specific controls
    struct v4l2_control control;
    CLEAR(control);
    control.id = 0x009a2009;  // gain
    control.value = 100;  // arbitrary value, adjust as needed
    if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
        perror("VIDIOC_S_CTRL for gain");
    }

    CLEAR(control);
    control.id = 0x009a200a;  // exposure
    control.value = 10000;  // arbitrary value, adjust as needed
    if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
        perror("VIDIOC_S_CTRL for exposure");
    }

    // Request buffers
    CLEAR(req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    printf("Requesting buffers...\n");
    if (-1 == ioctl(fd, VIDIOC_REQBUFS, &req)) {
        perror("VIDIOC_REQBUFS");
        exit(EXIT_FAILURE);
    }
    printf("Buffers requested successfully\n");

    buffers = static_cast<buffer*>(calloc(req.count, sizeof(*buffers)));
    if (!buffers) {
        perror("Out of memory");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        CLEAR(buf);

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = n_buffers;

        printf("Querying buffer %d...\n", n_buffers);
        if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &buf)) {
            perror("VIDIOC_QUERYBUF");
            exit(EXIT_FAILURE);
        }
        printf("Buffer %d queried successfully\n", n_buffers);

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length,
                      PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < n_buffers; ++i) {
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        printf("Queueing buffer %d...\n", i);
        if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)) {
            perror("VIDIOC_QBUF");
            exit(EXIT_FAILURE);
        }
        printf("Buffer %d queued successfully\n", i);
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("Starting stream...\n");
    if (-1 == ioctl(fd, VIDIOC_STREAMON, &type)) {
        perror("VIDIOC_STREAMON");
        exit(EXIT_FAILURE);
    }
    printf("Stream started successfully\n");

    sleep(2);

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    tv.tv_sec = 10;  // Increase timeout to 10 seconds
    tv.tv_usec = 0;

    for (int attempt = 0; attempt < 5; attempt++) {
        printf("Attempt %d: Waiting for frame (timeout: %ld seconds)...\n", attempt + 1, tv.tv_sec);
        r = select(fd + 1, &fds, NULL, NULL, &tv);

        if (-1 == r) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (0 == r) {
            fprintf(stderr, "select timeout\n");
        } else {
            printf("Frame is ready\n");
            break;
        }

        // Check stream status
        v4l2_input input;
        CLEAR(input);
        if (-1 == ioctl(fd, VIDIOC_G_INPUT, &input.index)) {
            perror("VIDIOC_G_INPUT");
        } else if (-1 == ioctl(fd, VIDIOC_ENUMINPUT, &input)) {
            perror("VIDIOC_ENUMINPUT");
        } else {
            printf("Current input status: 0x%08X\n", input.status);
        }
    }

    if (r == 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        printf("File descriptor flags: %d\n", flags);
        char buffer[4096];
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            perror("read");
        } else if (bytes_read == 0) {
            printf("End of file reached\n");
        } else {
            printf("Read %zd bytes from device\n", bytes_read);
        }
        exit(EXIT_FAILURE);
    }

    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    printf("Dequeuing buffer...\n");
    if (-1 == ioctl(fd, VIDIOC_DQBUF, &buf)) {
        perror("VIDIOC_DQBUF");
        exit(EXIT_FAILURE);
    }
    printf("Buffer dequeued successfully\n");

    snprintf(out_name, sizeof(out_name), "output_%ld.png", buf.timestamp.tv_sec);
    process_image(buffers[buf.index].start, buf.bytesused, out_name);

    printf("Queueing buffer...\n");
    if (-1 == ioctl(fd, VIDIOC_QBUF, &buf)) {
        perror("VIDIOC_QBUF");
        exit(EXIT_FAILURE);
    }
    printf("Buffer queued successfully\n");

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printf("Stopping stream...\n");
    if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &type)) {
        perror("VIDIOC_STREAMOFF");
        exit(EXIT_FAILURE);
    }
    printf("Stream stopped successfully\n");

    for (i = 0; i < n_buffers; ++i)
        munmap(buffers[i].start, buffers[i].length);

    close(fd);

    printf("Image saved as %s\n", out_name);

    return 0;
}