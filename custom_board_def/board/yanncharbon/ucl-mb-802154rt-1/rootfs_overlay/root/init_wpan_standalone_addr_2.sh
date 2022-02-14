#!/bin/sh
iwpan dev wpan0 set pan_id 0x0023
iwpan dev wpan0 set short_addr 0x0002
ifconfig wpan0 up
