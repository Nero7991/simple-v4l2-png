# V4L2 Camera Image Capture

This program captures an image from a camera using the Video4Linux2 (V4L2) API and saves it as a PNG file. It is written in C++ and utilizes the V4L2 API for camera control and the libpng library for PNG file creation.

## Prerequisites

Before compiling and running the program, make sure you have the following dependencies installed:

- C++ compiler (e.g., GCC)
- libv4l2 library
- libpng library

## Compilation

To compile the program, use the following command:

    gcc -o v4l2_png main.cpp -lv4l2 -lpng

This command compiles the `main.cpp` file and links it with the necessary libraries (`libv4l2` and `libpng`), generating an executable named `v4l2_png`.

## Usage

To run the program, execute the following command:

    ./v4l2_png

The program will open the default camera device (e.g., "/dev/video0"), capture a single frame, process the image data, and save it as a PNG file in the current directory. The output file will have a timestamp-based filename in the format `output_<timestamp>.png`.

## Customization

You can customize the program by modifying the following parameters in the code:

- `dev_name`: Change the camera device name if using a different device.
- `fmt.fmt.pix.width` and `fmt.fmt.pix.height`: Adjust the image resolution.
- `streamparm.parm.capture.timeperframe.denominator`: Modify the frame rate.

Note: Make sure the camera supports the specified resolution and frame rate.

## Troubleshooting

If you encounter any issues while running the program, consider the following:

- Check if the camera is properly connected and recognized by the system.
- Verify that you have the necessary permissions to access the camera device.
- Ensure that the required libraries (`libv4l2` and `libpng`) are installed correctly.

## License

This program is licensed under the MIT License.
