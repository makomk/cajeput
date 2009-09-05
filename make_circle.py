#!/usr/bin/python
import sys
from math import sin, cos, pi

for i in range(0, 8):
    angle = pi*2.0*(i/8.0)
    print "{ %f, %f }," % (sin(angle), cos(angle))
