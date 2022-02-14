#!/bin/sh
iwpan dev wpan0 set pan_id 0xbeef
ip link add link wpan0 name lowpan0 type lowpan
ifconfig wpan0 up
ifconfig lowpan0 up
