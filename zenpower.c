#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/amd_nb.h>

MODULE_DESCRIPTION("AMD ZEN family CPU Sensors Driver");
MODULE_AUTHOR("Ondrej Čerman");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.5");

// based on k10temp - GPL - (c) 2009 Clemens Ladisch <clemens@ladisch.de>
//
// Docs:
//  - https://www.kernel.org/doc/Documentation/hwmon/hwmon-kernel-api.txt
//  - https://developer.amd.com/wp-content/resources/56255_3_03.PDF
//
// Sources:
//  - Temp monitoring is from k10temp
//  - SVI address and voltage formula is from LibreHardwareMonitor
//  - Current formulas were discovered experimentally


#ifndef PCI_DEVICE_ID_AMD_17H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_DF_F3         0x1463
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M10H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M10H_DF_F3    0x15eb
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M30H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M30H_DF_F3    0x1493
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M70H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M70H_DF_F3    0x1443
#endif

#define F17H_M01H_REPORTED_TEMP_CTRL        0x00059800
#define F17H_M01H_SVI                       0x0005A000
#define F17H_M01H_SVI_TEL_PLANE0            F17H_M01H_SVI + 0xc
#define F17H_M01H_SVI_TEL_PLANE1            F17H_M01H_SVI + 0x10
#define F17H_M70H_CCD1_TEMP                 0x00059954
#define F17H_M70H_CCD2_TEMP                 0x00059958

#define F17H_TEMP_ADJUST_MASK               0x80000

struct zenpower_data {
	struct pci_dev *pdev;
	void (*read_amdsmn_addr)(struct pci_dev *pdev, u32 address, u32 *regval);
	u32 svi_core_addr;
	u32 svi_soc_addr;
	int temp_offset;
	bool zen2;
	bool kernel_smn_support;
	bool amps_visible;
	bool ccd1_visible, ccd2_visible;
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

static umode_t zenpower_is_visible(struct kobject *kobj,
				struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct zenpower_data *data = dev_get_drvdata(dev);

	switch (index) {
		case 4 ... 11:  // amperage and wattage
			if (!data->amps_visible)
				return 0;
			break;
		case 17 ... 18: // CCD1 temperature
			if (!data->ccd1_visible)
				return 0;
			break;
		case 19 ... 20: // CCD2 temperature
			if (!data->ccd2_visible)
				return 0;
			break;
	}

	return attr->mode;
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

	data->read_amdsmn_addr(data->pdev, F17H_M01H_REPORTED_TEMP_CTRL, &regval);
	temp = (regval >> 21) * 125;
	if (regval & F17H_TEMP_ADJUST_MASK)
		temp -= 49000;
	return temp;
}

static unsigned int get_ccd_temp(struct zenpower_data *data, u32 ccd_addr)
{
	u32 regval;
	data->read_amdsmn_addr(data->pdev, ccd_addr, &regval);

	return (regval & 0xfff) * 125 - 305000;
}

static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	unsigned int temp = get_ctl_temp(data);

	if (temp > data->temp_offset)
		temp -= data->temp_offset;
	else
		temp = 0;

	return sprintf(buf, "%u\n", temp);
}

static ssize_t temp1_max_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 70 * 1000);
}

static ssize_t temp2_input_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	unsigned int temp = get_ctl_temp(data);

	return sprintf(buf, "%u\n", temp);
}

static ssize_t temp3_input_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	unsigned int temp = get_ccd_temp(data, F17H_M70H_CCD1_TEMP);

	return sprintf(buf, "%u\n", temp);
}

static ssize_t temp4_input_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	unsigned int temp = get_ccd_temp(data, F17H_M70H_CCD2_TEMP);

	return sprintf(buf, "%u\n", temp);
}

static ssize_t in1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane, vcc;

	data->read_amdsmn_addr(data->pdev, data->svi_core_addr, &plane);
	vcc = plane_to_vcc(plane);

	return sprintf(buf, "%d\n", vcc);
}

static ssize_t in2_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane, vcc;

	data->read_amdsmn_addr(data->pdev, data->svi_soc_addr, &plane);
	vcc = plane_to_vcc(plane);

	return sprintf(buf, "%d\n", vcc);
}

static ssize_t curr1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_amdsmn_addr(data->pdev, data->svi_core_addr, &plane);
	return sprintf(buf, "%d\n", get_core_current(plane, data->zen2));
}

static ssize_t curr2_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_amdsmn_addr(data->pdev, data->svi_soc_addr, &plane);
	return sprintf(buf, "%d\n", get_soc_current(plane, data->zen2));
}

static ssize_t power1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_amdsmn_addr(data->pdev, data->svi_core_addr, &plane);
	return sprintf(buf, "%d\n",
		get_core_current(plane, data->zen2) * plane_to_vcc(plane));
}

static ssize_t power2_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_amdsmn_addr(data->pdev, data->svi_soc_addr, &plane);
	return sprintf(buf, "%d\n",
		get_soc_current(plane, data->zen2) * plane_to_vcc(plane));
}

int static debug_addrs_arr[] = {
	F17H_M01H_SVI + 0x8, F17H_M01H_SVI_TEL_PLANE0, F17H_M01H_SVI_TEL_PLANE1,
	0x000598BC, 0x0005994C, F17H_M70H_CCD1_TEMP, F17H_M70H_CCD2_TEMP, 0x0005995C
};

static ssize_t debug_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 smndata;

	len += sprintf(buf, "kernel_smn_support = %d\n", data->kernel_smn_support);
	for (i = 0; i < ARRAY_SIZE(debug_addrs_arr); i++){
		data->read_amdsmn_addr(data->pdev, debug_addrs_arr[i], &smndata);
		len += sprintf(buf + len, "%08x = %08x\n", debug_addrs_arr[i], smndata);
	}

	return len;
}


static ssize_t zen_label_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	switch (attr->index){
		case 1:
			return sprintf(buf, "SVI2_Core\n");
		case 2:
			return sprintf(buf, "SVI2_SoC\n");
		case 11:
			return sprintf(buf, "SVI2_C_Core\n");
		case 12:
			return sprintf(buf, "SVI2_C_SoC\n");
		case 21:
			return sprintf(buf, "SVI2_P_Core\n");
		case 22:
			return sprintf(buf, "SVI2_P_SoC\n");
		case 31:
			return sprintf(buf, "Tdie\n");
		case 32:
			return sprintf(buf, "Tctl\n");
		case 33:
			return sprintf(buf, "Tccd1\n");
		case 34:
			return sprintf(buf, "Tccd2\n");
	}

	return 0;
}

static DEVICE_ATTR_RO(in1_input);
static SENSOR_DEVICE_ATTR(in1_label, 0444, zen_label_show, NULL, 1);
static DEVICE_ATTR_RO(in2_input);
static SENSOR_DEVICE_ATTR(in2_label, 0444, zen_label_show, NULL, 2);
static DEVICE_ATTR_RO(curr1_input);
static SENSOR_DEVICE_ATTR(curr1_label, 0444, zen_label_show, NULL, 11);
static DEVICE_ATTR_RO(curr2_input);
static SENSOR_DEVICE_ATTR(curr2_label, 0444, zen_label_show, NULL, 12);
static DEVICE_ATTR_RO(power1_input);
static SENSOR_DEVICE_ATTR(power1_label, 0444, zen_label_show, NULL, 21);
static DEVICE_ATTR_RO(power2_input);
static SENSOR_DEVICE_ATTR(power2_label, 0444, zen_label_show, NULL, 22);
static DEVICE_ATTR_RO(temp1_input);
static DEVICE_ATTR_RO(temp1_max);
static SENSOR_DEVICE_ATTR(temp1_label, 0444, zen_label_show, NULL, 31);
static DEVICE_ATTR_RO(temp2_input);
static SENSOR_DEVICE_ATTR(temp2_label, 0444, zen_label_show, NULL, 32);
static DEVICE_ATTR_RO(temp3_input);
static SENSOR_DEVICE_ATTR(temp3_label, 0444, zen_label_show, NULL, 33);
static DEVICE_ATTR_RO(temp4_input);
static SENSOR_DEVICE_ATTR(temp4_label, 0444, zen_label_show, NULL, 34);
static DEVICE_ATTR_RO(debug_data);


static struct attribute *zenpower_attrs[] = {
	&dev_attr_in1_input.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&dev_attr_in2_input.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,
	&dev_attr_curr1_input.attr,
	&sensor_dev_attr_curr1_label.dev_attr.attr,
	&dev_attr_curr2_input.attr,
	&sensor_dev_attr_curr2_label.dev_attr.attr,
	&dev_attr_power1_input.attr,
	&sensor_dev_attr_power1_label.dev_attr.attr,
	&dev_attr_power2_input.attr,
	&sensor_dev_attr_power2_label.dev_attr.attr,
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_max.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&dev_attr_temp2_input.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	&dev_attr_temp3_input.attr,
	&sensor_dev_attr_temp3_label.dev_attr.attr,
	&dev_attr_temp4_input.attr,
	&sensor_dev_attr_temp4_label.dev_attr.attr,
	&dev_attr_debug_data.attr,
	NULL
};

static const struct attribute_group zenpower_group = {
	.attrs = zenpower_attrs,
	.is_visible = zenpower_is_visible,
};
__ATTRIBUTE_GROUPS(zenpower);


static void kernel_smn_read(struct pci_dev *pdev, u32 address, u32 *regval)
{
	amd_smn_read(amd_pci_dev_to_node_id(pdev), address, regval);
}

// fallback method from k10temp
// may return inaccurate results on multi-die chips
static void nb_index_read(struct pci_dev *pdev, u32 address, u32 *regval)
{
	mutex_lock(&nb_smu_ind_mutex);
	pci_bus_write_config_dword(pdev->bus, PCI_DEVFN(0, 0), 0x60, address);
	pci_bus_read_config_dword(pdev->bus, PCI_DEVFN(0, 0), 0x64, regval);
	mutex_unlock(&nb_smu_ind_mutex);
}

static int zenpower_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct zenpower_data *data;
	struct device *hwmon_dev;
	bool swapped_addr = false;
	u32 tmp;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->zen2 = false;
	data->pdev = pdev;
	data->read_amdsmn_addr = nb_index_read;
	data->kernel_smn_support = false;
	data->amps_visible = false;
	data->ccd1_visible = false;
	data->ccd2_visible = false;

	for (id = amd_nb_misc_ids; id->vendor; id++) {
		if (pdev->vendor == id->vendor && pdev->device == id->device) {
			data->kernel_smn_support = true;
			data->read_amdsmn_addr = kernel_smn_read;
			break;
		}
	}

	if (boot_cpu_data.x86 == 0x17) {
		switch (boot_cpu_data.x86_model) {
			case 0x1:  // Zen
			case 0x8:  // Zen+
			case 0x11: // Zen APU
			case 0x18: // Zen+ APU
				data->amps_visible = true;
				break;

			case 0x71: // Zen2
				data->amps_visible = true;
				data->zen2 = true;
				swapped_addr = true;

				data->read_amdsmn_addr(pdev, F17H_M70H_CCD1_TEMP, &tmp);
				if ((tmp & 0xfff) > 0) {
					data->ccd1_visible = true;
				}

				data->read_amdsmn_addr(pdev, F17H_M70H_CCD2_TEMP, &tmp);
				if ((tmp & 0xfff) > 0) {
					data->ccd2_visible = true;
				}
				break;
		}
	}

	#ifdef SWAP_CORE_SOC
		swapped_addr = !swapped_addr;
	#endif

	if (swapped_addr) {
		data->svi_core_addr = F17H_M01H_SVI_TEL_PLANE1;
		data->svi_soc_addr = F17H_M01H_SVI_TEL_PLANE0;
	}
	else {
		data->svi_core_addr = F17H_M01H_SVI_TEL_PLANE0;
		data->svi_soc_addr = F17H_M01H_SVI_TEL_PLANE1;
	}

	for (i = 0; i < ARRAY_SIZE(tctl_offset_table); i++) {
		const struct tctl_offset *entry = &tctl_offset_table[i];

		if (boot_cpu_data.x86 == entry->model &&
			strstr(boot_cpu_data.x86_model_id, entry->id)) {
			data->temp_offset = entry->offset;
			break;
		}
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, "zenpower", data, zenpower_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct pci_device_id zenpower_id_table[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M10H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M30H_DF_F3) },
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
