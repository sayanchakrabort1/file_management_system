# filemanagement
File Management System in CPP including GUI

Optimized for WSL and Linux distribution

Requirements:
Ubuntu 22 or higher

Steps to run:
1. File is ready to run and execute
2. Install GTK in your Linux ( sudo apt install libgtk-3-dev )
3. Direct to the folder where the file_index.cpp file is kept
4. execute this command - ``` g++ `pkg-config --cflags gtk+-3.0` -o file_index file_index.cpp `pkg-config --libs gtk+-3.0` ```
5. after compilation is done run the code with the following command - ``` ./file_index ```
