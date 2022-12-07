#!/bin/bash

mkdir build

cd build || exit

cmake ..

cd ..

make

./ddriver_test

cd - || exit