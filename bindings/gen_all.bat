@echo off
python gen.py ../include/rafx.h --lang rust -o rafx-rs
python gen.py ../include/rafx.h --lang odin -o rafx-odin
python gen.py ../include/rafx.h --lang jai -o rafx-jai
