/*
 * Zenpower - Driver for reading temperature, voltage, current and power for AMD 17h CPUs
 *
 * Copyright (c) 2018-2020 Ondrej Čerman
 *
 * This driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * based on k10temp by Clemens Ladisch
 *
 * Docs:
 *  - https://www.kernel.org/doc/Documentation/hwmon/hwmon-kernel-api.txt
 *  - https://developer.amd.com/wp-content/resources/56255_3_03.PDF
 *
 * Sources:
 *  - Temp monitoring is from k10temp
 *  - SVI address and voltage formula is from LibreHardwareMonitor
 *  - Current formulas and CCD temp addresses were discovered experimentally
 */

#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/amd_nb.h>

MODULE_DESCRIPTION("AMD ZEN family CPU Sensors Driver");
MODULE_AUTHOR("Ondrej Čerman");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.12");


#ifndef PCI_DEVICE_ID_AMD_17H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_DF_F3         0x1463
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M10H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M10H_DF_F3    0x15eb
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M30H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M30H_DF_F3    0x1493
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M60H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M60H_DF_F3    0x144b
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M70H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M70H_DF_F3    0x1443
#endif

#define F17H_M01H_REPORTED_TEMP_CTRL        0x00059800
#define F17H_M01H_SVI                       0x0005A000
#define F17H_M01H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0xC)
#define F17H_M01H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0x10)
#define F17H_M30H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x14)
#define F17H_M30H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0x10)
#define F17H_M70H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x10)
#define F17H_M70H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0xC)
#define F17H_M70H_CCD_TEMP(x)               (0x00059954 + ((x) * 4))

#define F17H_TEMP_ADJUST_MASK               0x80000

#ifndef HWMON_CHANNEL_INFO
#define HWMON_CHANNEL_INFO(stype, ...)	\
	(&(struct hwmon_channel_info) {		\
		.type = hwmon_##stype,			\
		.config = (u32 []) {			\
			__VA_ARGS__, 0				\
		}								\
	})
#endif

struct zenpower_data {
	struct pci_dev *pdev;
	void (*read_amdsmn_addr)(struct pci_dev *pdev, u16 node_id, u32 address, u32 *regval);
	u32 svi_core_addr;
	u32 svi_soc_addr;
	u16 node_id;
	u8 cpu_id;
	u8 nodes_per_cpu;
	int temp_offset;
	bool zen2;
	bool kernel_smn_support;
	bool amps_visible;
	bool ccd_visible[8];
};

struct tctl_offset {
	u8 model;
	char const *id;
	int offset;
};

static const struct tctl_offset tctl_offset_table[] = {
	{ 0x17, "AMD Ryzen 5 1600X", 20000 },
	{ 0x17, "AMD Ryzen 7 1700X", 20000 },
	{ 0x17, "AMD Ryzen 7 1800X", 20000 },
	{ 0x17, "AMD Ryzen 7 2700X", 10000 },
	{ 0x17, "AMD Ryzen Threadripper 19", 27000 }, /* 19{00,20,50}X */
	{ 0x17, "AMD Ryzen Threadripper 29", 27000 }, /* 29{20,50,70,90}[W]X */
};

static DEFINE_MUTEX(nb_smu_ind_mutex);
static bool multicpu = false;

static umode_t zenpower_is_visible(const void *rdata,
									enum hwmon_sensor_types type,
									u32 attr, int channel)
{
	const struct zenpower_data *data = rdata;

	switch (type) {
		case hwmon_temp:
			if (channel >= 2 && data->ccd_visible[channel-2] == false) // Tccd1-8
				return 0;
			break;

		case hwmon_curr:
		case hwmon_power:
			if (data->amps_visible == false)
				return 0;
			if (channel == 0 && data->svi_core_addr == 0)
				return 0;
			if (channel == 1 && data->svi_soc_addr == 0)
				return 0;
			break;

		case hwmon_in:
			if (channel == 0)	// fake item to align different indexing,
				return 0;		// see note at zenpower_info
			if (channel == 1 && data->svi_core_addr == 0)
				return 0;
			if (channel == 2 && data->svi_soc_addr == 0)
				return 0;
			break;

		default:
			break;
	}

	return 0444;
}

static u32 plane_to_vcc(u32 p)
{
	u32 vdd_cor;
	vdd_cor = (p >> 16) & 0xff;
	// U = 1550 - 6.25 * vddcor

	return  1550 - ((625 * vdd_cor) / 100);
}

static u32 get_core_current(u32 plane, bool zen2)
{
	u32 idd_cor, fc;
	idd_cor = plane & 0xff;

	// I = 1039.211 * iddcor
	// I =  658.823 * iddcor
	fc = zen2 ? 658823 : 1039211;

	return  (fc * idd_cor) / 1000;
}

static u32 get_soc_current(u32 plane, bool zen2)
{
	u32 idd_cor, fc;
	idd_cor = plane & 0xff;

	// I = 360.772 * iddcor
	// I = 294.3   * iddcor
	fc = zen2 ? 294300 : 360772;

	return  (fc * idd_cor) / 1000;
}

static unsigned int get_ctl_temp(struct zenpower_data *data)
{
	unsigned int temp;
	u32 regval;

	data->read_amdsmn_addr(data->pdev, data->node_id,
							F17H_M01H_REPORTED_TEMP_CTRL, &regval);
	temp = (regval >> 21) * 125;
	if (regval & F17H_TEMP_ADJUST_MASK)
		temp -= 49000;
	return temp;
}

static unsigned int get_ccd_temp(struct zenpower_data *data, u32 ccd_addr)
{
	u32 regval;
	data->read_amdsmn_addr(data->pdev, data->node_id, ccd_addr, &regval);

	return (regval & 0xfff) * 125 - 305000;
}

int static debug_addrs_arr[] = {
	F17H_M01H_SVI + 0x8, F17H_M01H_SVI + 0xC, F17H_M01H_SVI + 0x10,
	F17H_M01H_SVI + 0x14, 0x000598BC, 0x0005994C, F17H_M70H_CCD_TEMP(0),
	F17H_M70H_CCD_TEMP(1), F17H_M70H_CCD_TEMP(2), F17H_M70H_CCD_TEMP(3),
	F17H_M70H_CCD_TEMP(4), F17H_M70H_CCD_TEMP(5), F17H_M70H_CCD_TEMP(6),
	F17H_M70H_CCD_TEMP(7)
};

static ssize_t debug_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 smndata;

	len += sprintf(buf + len, "KERN_SUP: %d\n", data->kernel_smn_support);
	len += sprintf(buf + len, "NODE%d; CPU%d; ", data->node_id, data->cpu_id);
	len += sprintf(buf + len, "N/CPU: %d\n", data->nodes_per_cpu);

	for (i = 0; i < ARRAY_SIZE(debug_addrs_arr); i++){
		data->read_amdsmn_addr(data->pdev, data->node_id, debug_addrs_arr[i], &smndata);
		len += sprintf(buf + len, "%08x = %08x\n", debug_addrs_arr[i], smndata);
	}

	return len;
}

static int zenpower_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	switch (type) {

		// Temperatures
		case hwmon_temp:
			switch (attr) {
				case hwmon_temp_input:
					switch (channel) {
						case 0: // Tdie
							*val = get_ctl_temp(data) - data->temp_offset;
							break;
						case 1: // Tctl
							*val = get_ctl_temp(data);
							break;
						case 2 ... 9: // Tccd1-8
							*val = get_ccd_temp(data, F17H_M70H_CCD_TEMP(channel-2));
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;

				case hwmon_temp_max: // Tdie max
					// source: https://www.amd.com/en/products/cpu/amd-ryzen-7-3700x
					//         other cpus have also same* Tmax on AMD website
					//         * = when taking into consideration a tctl offset
					*val = 95 * 1000;
					break;

				default:
					return -EOPNOTSUPP;
			}
			break;

		// Voltage
		case hwmon_in:
			if (channel == 0)
				return -EOPNOTSUPP;
			channel -= 1;	// hwmon_in have different indexing, see note at zenpower_info
							// fall through
		// Power / Current
		case hwmon_curr:
		case hwmon_power:
			if (attr != hwmon_in_input && attr != hwmon_curr_input &&
				attr != hwmon_power_input) {
				return -EOPNOTSUPP;
			}

			switch (channel) {
				case 0: // Core SVI2
					data->read_amdsmn_addr(data->pdev, data->node_id,
											data->svi_core_addr, &plane);
					break;
				case 1: // SoC SVI2
					data->read_amdsmn_addr(data->pdev, data->node_id,
											data->svi_soc_addr, &plane);
					break;
				default:
					return -EOPNOTSUPP;
			}

			switch (type) {
				case hwmon_in:
					*val = plane_to_vcc(plane);
					break;
				case hwmon_curr:
					*val = (channel == 0) ?
						get_core_current(plane, data->zen2):
						get_soc_current(plane, data->zen2);
					break;
				case hwmon_power:
					*val = (channel == 0) ?
						get_core_current(plane, data->zen2) * plane_to_vcc(plane):
						get_soc_current(plane, data->zen2) * plane_to_vcc(plane);
					break;
				default:
					break;
			}
			break;

		default:
			return -EOPNOTSUPP;
	}

	return 0;
}

static const char *zenpower_temp_label[][10] = {
	{
		"Tdie",
		"Tctl",
		"Tccd1",
		"Tccd2",
		"Tccd3",
		"Tccd4",
		"Tccd5",
		"Tccd6",
		"Tccd7",
		"Tccd8",
	},
	{
		"cpu0 Tdie",
		"cpu0 Tctl",
		"cpu0 Tccd1",
		"cpu0 Tccd2",
		"cpu0 Tccd3",
		"cpu0 Tccd4",
		"cpu0 Tccd5",
		"cpu0 Tccd6",
		"cpu0 Tccd7",
		"cpu0 Tccd8",
	},
	{
		"cpu1 Tdie",
		"cpu1 Tctl",
		"cpu1 Tccd1",
		"cpu1 Tccd2",
		"cpu1 Tccd3",
		"cpu1 Tccd4",
		"cpu1 Tccd5",
		"cpu1 Tccd6",
		"cpu1 Tccd7",
		"cpu1 Tccd8",
	}
};

static const char *zenpower_in_label[][3] = {
	{
		"",
		"SVI2_Core",
		"SVI2_SoC",
	},
	{
		"",
		"cpu0 SVI2_Core",
		"cpu0 SVI2_SoC",
	},
	{
		"",
		"cpu1 SVI2_Core",
		"cpu1 SVI2_SoC",
	}
};

static const char *zenpower_curr_label[][2] = {
	{
		"SVI2_C_Core",
		"SVI2_C_SoC",
	},
	{
		"cpu0 SVI2_C_Core",
		"cpu0 SVI2_C_SoC",
	},
	{
		"cpu1 SVI2_C_Core",
		"cpu1 SVI2_C_SoC",
	}
};

static const char *zenpower_power_label[][2] = {
	{
		"SVI2_P_Core",
		"SVI2_P_SoC",
	},
	{
		"cpu0 SVI2_P_Core",
		"cpu0 SVI2_P_SoC",
	},
	{
		"cpu1 SVI2_P_Core",
		"cpu1 SVI2_P_SoC",
	}
};

static int zenpower_read_labels(struct device *dev,
				enum hwmon_sensor_types type, u32 attr,
				int channel, const char **str)
{
	struct zenpower_data *data;
	u8 i = 0;

	if (multicpu) {
		data = dev_get_drvdata(dev);
		if (data->cpu_id <= 1)
			i = data->cpu_id + 1;
	}

	switch (type) {
		case hwmon_temp:
			*str = zenpower_temp_label[i][channel];
			break;
		case hwmon_in:
			*str = zenpower_in_label[i][channel];
			break;
		case hwmon_curr:
			*str = zenpower_curr_label[i][channel];
			break;
		case hwmon_power:
			*str = zenpower_power_label[i][channel];
			break;
		default:
			return -EOPNOTSUPP;
	}

	return 0;
}

static void kernel_smn_read(struct pci_dev *pdev, u16 node_id, u32 address, u32 *regval)
{
	amd_smn_read(node_id, address, regval);
}

// fallback method from k10temp
// may return inaccurate results on multi-die chips
static void nb_index_read(struct pci_dev *pdev, u16 node_id, u32 address, u32 *regval)
{
	mutex_lock(&nb_smu_ind_mutex);
	pci_bus_write_config_dword(pdev->bus, PCI_DEVFN(0, 0), 0x60, address);
	pci_bus_read_config_dword(pdev->bus, PCI_DEVFN(0, 0), 0x64, regval);
	mutex_unlock(&nb_smu_ind_mutex);
}

static const struct hwmon_channel_info *zenpower_info[] = {
	HWMON_CHANNEL_INFO(temp,
			HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_LABEL,	// Tdie
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tctl
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd1
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd2
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd3
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd4
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd5
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd6
			HWMON_T_INPUT | HWMON_T_LABEL,					// Tccd7
			HWMON_T_INPUT | HWMON_T_LABEL),					// Tccd8

	HWMON_CHANNEL_INFO(in,
			HWMON_I_LABEL,	// everything is using 1 based indexing except
							// hwmon_in - that is using 0 based indexing
							// let's make fake item so corresponding SVI2 data is
							// associated with same index
			HWMON_I_INPUT | HWMON_I_LABEL,		// Core Voltage (SVI2)
			HWMON_I_INPUT | HWMON_I_LABEL),		// SoC Voltage (SVI2)

	HWMON_CHANNEL_INFO(curr,
			HWMON_C_INPUT | HWMON_C_LABEL,		// Core Current (SVI2)
			HWMON_C_INPUT | HWMON_C_LABEL),		// SoC Current (SVI2)

	HWMON_CHANNEL_INFO(power,
			HWMON_P_INPUT | HWMON_P_LABEL,		// Core Power (SVI2)
			HWMON_P_INPUT | HWMON_P_LABEL),		// SoC Power (SVI2)

	NULL
};

static const struct hwmon_ops zenpower_hwmon_ops = {
	.is_visible = zenpower_is_visible,
	.read = zenpower_read,
	.read_string = zenpower_read_labels,
};

static const struct hwmon_chip_info zenpower_chip_info = {
	.ops = &zenpower_hwmon_ops,
	.info = zenpower_info,
};

static DEVICE_ATTR_RO(debug_data);

static struct attribute *zenpower_attrs[] = {
	&dev_attr_debug_data.attr,
	NULL
};

static const struct attribute_group zenpower_group = {
	.attrs = zenpower_attrs
};
__ATTRIBUTE_GROUPS(zenpower);

static int zenpower_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct zenpower_data *data;
	struct device *hwmon_dev;
	struct pci_dev *misc;
	int i, ccd_check = 0;
	bool multinode;
	u8 node_of_cpu;
	u32 val;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->zen2 = false;
	data->pdev = pdev;
	data->temp_offset = 0;
	data->read_amdsmn_addr = nb_index_read;
	data->kernel_smn_support = false;
	data->svi_core_addr = false;
	data->svi_soc_addr = false;
	data->amps_visible = false;
	data->node_id = 0;
	for (i = 0; i < 8; i++) {
		data->ccd_visible[i] = false;
	}

	for (i = 0; i < amd_nb_num(); i++) {
		misc = node_to_amd_nb(i)->misc;
		if (pdev->vendor == misc->vendor && pdev->device == misc->device) {
			data->kernel_smn_support = true;
			data->read_amdsmn_addr = kernel_smn_read;
			data->node_id = amd_pci_dev_to_node_id(pdev);
			break;
		}
	}

	// CPUID_Fn8000001E_ECX [Node Identifiers] (Core::X86::Cpuid::NodeId)
	// 10:8 NodesPerProcessor
	data->nodes_per_cpu = 1 + ((cpuid_ecx(0x8000001E) >> 8) & 0b111);
	multinode = (data->nodes_per_cpu > 1);

	node_of_cpu = data->node_id % data->nodes_per_cpu;
	data->cpu_id = data->node_id / data->nodes_per_cpu;

	if (data->cpu_id > 0)
		multicpu = true;

	if (boot_cpu_data.x86 == 0x17) {
		switch (boot_cpu_data.x86_model) {
			case 0x1:  // Zen
			case 0x8:  // Zen+
				data->amps_visible = true;

				if (multinode) {	// Threadripper / EPYC
					if (node_of_cpu == 0) {
						data->svi_soc_addr = F17H_M01H_SVI_TEL_PLANE0;
					}
					if (node_of_cpu == 1) {
						data->svi_core_addr = F17H_M01H_SVI_TEL_PLANE0;
					}
				}
				else {				// Ryzen
					data->svi_core_addr = F17H_M01H_SVI_TEL_PLANE0;
					data->svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1;
				}
				ccd_check = 4;
				break;

			case 0x11: // Zen APU
			case 0x18: // Zen+ APU
				data->amps_visible = true;
				data->svi_core_addr = F17H_M01H_SVI_TEL_PLANE0;
				data->svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1;
				break;

			case 0x31: // Zen2 Threadripper/EPYC
				data->zen2 = true;
				data->amps_visible = true;
				data->svi_core_addr = F17H_M30H_SVI_TEL_PLANE0;
				data->svi_soc_addr = F17H_M30H_SVI_TEL_PLANE1;
				ccd_check = 8;
				break;

			case 0x60: // Zen2 APU
				data->zen2 = true;
				break;

			case 0x71: // Zen2 Ryzen
				data->zen2 = true;
				data->amps_visible = true;
				data->svi_core_addr = F17H_M70H_SVI_TEL_PLANE0;
				data->svi_soc_addr = F17H_M70H_SVI_TEL_PLANE1;
				ccd_check = 8;
				break;

			default:
				data->svi_core_addr = F17H_M01H_SVI_TEL_PLANE0;
				data->svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1;
				break;
		}
	}

	for (i = 0; i < ccd_check; i++) {
		data->read_amdsmn_addr(pdev, data->node_id,
								F17H_M70H_CCD_TEMP(i), &val);
		if ((val & 0xfff) > 0) {
			data->ccd_visible[i] = true;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tctl_offset_table); i++) {
		const struct tctl_offset *entry = &tctl_offset_table[i];

		if (boot_cpu_data.x86 == entry->model &&
			strstr(boot_cpu_data.x86_model_id, entry->id)) {
			data->temp_offset = entry->offset;
			break;
		}
	}

	hwmon_dev = devm_hwmon_device_register_with_info(
		dev, "zenpower", data, &zenpower_chip_info, zenpower_groups
	);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct pci_device_id zenpower_id_table[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M10H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M30H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M60H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M70H_DF_F3) },
	{}
};
MODULE_DEVICE_TABLE(pci, zenpower_id_table);

static struct pci_driver zenpower_driver = {
	.name = "zenpower",
	.id_table = zenpower_id_table,
	.probe = zenpower_probe,
};

module_pci_driver(zenpower_driver);
