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
#include <cerrno>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <png.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

void ahd_debayer(const uint16_t *src, uint8_t *row, int width, int height, int y)
{
    // Temporary buffers for horizontal and vertical interpolations
    uint16_t *h_interp = (uint16_t *)malloc(width * 3 * sizeof(uint16_t));
    uint16_t *v_interp = (uint16_t *)malloc(width * 3 * sizeof(uint16_t));

    // Perform horizontal and vertical interpolations
    for (int x = 0; x < width; x++) {
        int is_green = ((x + y) % 2 == 0);
        int is_blue = (y % 2 == 1 && x % 2 == 0);
        int is_red = (y % 2 == 0 && x % 2 == 1);

        // Horizontal interpolation
        if (is_green) {
            h_interp[x*3 + 1] = src[y * width + x] & 0x03FF;
            h_interp[x*3 + 0] = (x > 0) ? (src[y * width + x - 1] & 0x03FF) : h_interp[x*3 + 1];
            h_interp[x*3 + 2] = (x < width - 1) ? (src[y * width + x + 1] & 0x03FF) : h_interp[x*3 + 1];
        } else if (is_blue) {
            h_interp[x*3 + 2] = src[y * width + x] & 0x03FF;
            h_interp[x*3 + 1] = (x > 0 && x < width - 1) ? 
                ((src[y * width + x - 1] & 0x03FF) + (src[y * width + x + 1] & 0x03FF)) / 2 : h_interp[x*3 + 2];
            h_interp[x*3 + 0] = h_interp[x*3 + 1];
        } else { // is_red
            h_interp[x*3 + 0] = src[y * width + x] & 0x03FF;
            h_interp[x*3 + 1] = (x > 0 && x < width - 1) ? 
                ((src[y * width + x - 1] & 0x03FF) + (src[y * width + x + 1] & 0x03FF)) / 2 : h_interp[x*3 + 0];
            h_interp[x*3 + 2] = h_interp[x*3 + 1];
        }

        // Vertical interpolation (similar to horizontal, but using y-1 and y+1)
        if (is_green) {
            v_interp[x*3 + 1] = src[y * width + x] & 0x03FF;
            v_interp[x*3 + 0] = (y > 0) ? (src[(y - 1) * width + x] & 0x03FF) : v_interp[x*3 + 1];
            v_interp[x*3 + 2] = (y < height - 1) ? (src[(y + 1) * width + x] & 0x03FF) : v_interp[x*3 + 1];
        } else if (is_blue) {
            v_interp[x*3 + 2] = src[y * width + x] & 0x03FF;
            v_interp[x*3 + 1] = (y > 0 && y < height - 1) ? 
                ((src[(y - 1) * width + x] & 0x03FF) + (src[(y + 1) * width + x] & 0x03FF)) / 2 : v_interp[x*3 + 2];
            v_interp[x*3 + 0] = v_interp[x*3 + 1];
        } else { // is_red
            v_interp[x*3 + 0] = src[y * width + x] & 0x03FF;
            v_interp[x*3 + 1] = (y > 0 && y < height - 1) ? 
                ((src[(y - 1) * width + x] & 0x03FF) + (src[(y + 1) * width + x] & 0x03FF)) / 2 : v_interp[x*3 + 0];
            v_interp[x*3 + 2] = v_interp[x*3 + 1];
        }

        // Calculate homogeneity
        float h_homogeneity = 0, v_homogeneity = 0;
        for (int c = 0; c < 3; c++) {
            h_homogeneity += fabs(h_interp[MAX(0, x-1)*3 + c] - h_interp[x*3 + c]) + 
                             fabs(h_interp[MIN(width-1, x+1)*3 + c] - h_interp[x*3 + c]);
            v_homogeneity += fabs(v_interp[MAX(0, x-1)*3 + c] - v_interp[x*3 + c]) + 
                             fabs(v_interp[MIN(width-1, x+1)*3 + c] - v_interp[x*3 + c]);
        }

        // Choose interpolation with better homogeneity
        uint16_t *chosen = (h_homogeneity <= v_homogeneity) ? h_interp : v_interp;

        // Write to output
        row[x * 3] = chosen[x*3 + 0] >> 2;
        row[x * 3 + 1] = chosen[x*3 + 1] >> 2;
        row[x * 3 + 2] = chosen[x*3 + 2] >> 2;
    }

    free(h_interp);
    free(v_interp);
}

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
    void   *start;
    size_t length;
};

static void process_image(const void *p, int size, const char *filename, int width, int height) {
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

    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep row = (png_bytep)malloc(3 * width * sizeof(uint8_t));
    if (!row) {
        perror("Error allocating memory for row");
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    const uint16_t *src = (const uint16_t *)p;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t r, g, b;
            if (y % 2 == 0) {
                if (x % 2 == 0) {
                    r = src[y * width + x] & 0x03FF;
                    g = (uint16_t)(((uint32_t)(src[y * width + x + (x < width - 1 ? 1 : -1)] & 0x03FF) +
                        (uint32_t)(src[(y < height - 1 ? y + 1 : y - 1) * width + x] & 0x03FF)) >> 1);
                    b = src[(y < height - 1 ? y + 1 : y - 1) * width + 
                            (x < width - 1 ? x + 1 : x - 1)] & 0x03FF;
                } else {
                    r = (uint16_t)(((uint32_t)(src[y * width + x - 1] & 0x03FF) +
                        (uint32_t)(src[y * width + (x < width - 1 ? x + 1 : x - 1)] & 0x03FF)) >> 1);
                    g = src[y * width + x] & 0x03FF;
                    b = (uint16_t)(((uint32_t)(src[(y < height - 1 ? y + 1 : y - 1) * width + x - 1] & 0x03FF) +
                        (uint32_t)(src[(y < height - 1 ? y + 1 : y - 1) * width + 
                            (x < width - 1 ? x + 1 : x - 1)] & 0x03FF)) >> 1);
                }
            } else {
                if (x % 2 == 0) {
                    r = (uint16_t)(((uint32_t)(src[(y > 0 ? y - 1 : y + 1) * width + x] & 0x03FF) +
                        (uint32_t)(src[(y < height - 1 ? y + 1 : y) * width + x] & 0x03FF)) >> 1);
                    g = src[y * width + x] & 0x03FF;
                    b = (uint16_t)(((uint32_t)(src[y * width + x - 1] & 0x03FF) +
                        (uint32_t)(src[y * width + (x < width - 1 ? x + 1 : x - 1)] & 0x03FF)) >> 1);
                } else {
                    r = src[(y > 0 ? y - 1 : 0) * width + 
                            (x < width - 1 ? x + 1 : x)] & 0x03FF;
                    g = (uint16_t)(((uint32_t)(src[(y > 0 ? y - 1 : y + 1) * width + x] & 0x03FF) +
                        (uint32_t)(src[y * width + (x < width - 1 ? x + 1 : x - 1)] & 0x03FF)) >> 1);
                    b = src[y * width + x] & 0x03FF;
                }
            }
            row[x * 3] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        png_write_row(png, row);
    }

    free(row);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}

static void process_buffer(void *p, int size, int width, int height){
    uint16_t *src = (uint16_t *)p;
    for (uint16_t i = 0; i < width * height; i+=1){
        src[i] = src[i] << 6;
    }

}


static void process_image_rgb(const char *filename, int width, int height, uint8_t r, uint8_t g, uint8_t b) {
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
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    
    png_bytep row = (png_bytep)malloc(3 * width * sizeof(uint8_t));
    if (!row) {
        perror("Error allocating memory for row");
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            row[x * 3] = r;      // Red channel set to maximum (255)
            row[x * 3 + 1] = g;    // Green channel set to 0
            row[x * 3 + 2] = b;    // Blue channel set to 0
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
    fmt.fmt.pix.width       = 1920;
    fmt.fmt.pix.height      = 1080;
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
    streamparm.parm.capture.timeperframe.denominator = 15;

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

    sleep(1);

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
    process_image(buffers[buf.index].start, buf.bytesused, out_name, fmt.fmt.pix.width, fmt.fmt.pix.height);
    // process_image_rgb(out_name, fmt.fmt.pix.width, fmt.fmt.pix.height, 0, 0, 0xFF);

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