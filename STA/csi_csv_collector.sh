. $HOME/esp/esp-idf/export.sh
idf.py fullclean
idf.py build
#idf.py -p /dev/ttyUSB1 flash monitor
idf.py -p /dev/ttyUSB1 flash monitor |grep serial_num |tee /home/pml/DATA_CSI/CSI_STA.csv



