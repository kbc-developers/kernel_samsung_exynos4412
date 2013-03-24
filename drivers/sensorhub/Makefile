#
# Makefile for the sensor drivers.
#

# Each configuration option enables a list of files.
obj-$(CONFIG_SENSORS_SSP)		+= ssp_dev.o ssp_i2c.o ssp_data.o ssp_sysfs.o\
						ssp_input.o ssp_firmware.o ssp_debug.o

obj-$(CONFIG_SENSORS_SYSFS)		+= sensors_core.o

obj-$(CONFIG_SENSORS_SSP_LSM330)	+= factory/

obj-$(CONFIG_SENSORS_SSP_CM36651)	+= factory/

obj-$(CONFIG_SENSORS_SSP_AK8963C)	+= ssp_ak8963c.o factory/

obj-$(CONFIG_SENSORS_SSP_BMP182)	+= factory/

obj-$(CONFIG_SENSORS_SSP_AT32UC3L0128)	+= factory/

obj-$(CONFIG_SENSORS_SSP_SENSORHUB)	+= ssp_sensorhub.o
