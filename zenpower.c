#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/amd_nb.h>

MODULE_DESCRIPTION("Voltage monitor for AMD ZEN family CPUs");
MODULE_AUTHOR("Ondrej Čerman");
MODULE_LICENSE("GPL");

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
#define PCI_DEVICE_ID_AMD_17H_DF_F3	        0x1463
#endif

#ifndef PCI_DEVICE_ID_AMD_17H_M10H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_M10H_DF_F3	0x15eb
#endif

/* F17h M01h Access througn SMN */
#define F17H_M01H_REPORTED_TEMP_CTRL_OFFSET	0x00059800
#define F17H_M01H_SVI	                    0x0005A000

#define F17H_TEMP_ADJUST_MASK               0x80000

struct zenpower_data {
	struct pci_dev *pdev;
	void (*read_smusvi0_tel_plane0)(struct pci_dev *pdev, u32 *regval);
	void (*read_smusvi0_tel_plane1)(struct pci_dev *pdev, u32 *regval);
	void (*read_tempreg)(struct pci_dev *pdev, u32 *regval);
	int temp_offset;
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

static umode_t zenpower_is_visible(struct kobject *kobj,
				struct attribute *attr, int index)
{
	return attr->mode;
}

static u32 plane_to_vcc(u32 p)
{
	u32 vdd_cor;
	vdd_cor = (p >> 16) & 0xff;
	// U = 1550 - 6.25 * cdd_cor

	return  1550 - ((625 * vdd_cor) / 100);
}

static u32 get_core_current(u32 plane)
{
	u32 idd_cor;
	idd_cor = plane & 0xff;
	// I = 1039.211 * iddcor

	return  (1039211 * idd_cor) / 1000;
}

static u32 get_soc_current(u32 plane)
{
	u32 idd_cor;
	idd_cor = plane & 0xff;
	// I = 360.772 * iddcor

	return  (360772 * idd_cor) / 1000;
}

static unsigned int get_raw_temp(struct zenpower_data *data)
{
	unsigned int temp;
	u32 regval;

	data->read_tempreg(data->pdev, &regval);
	temp = (regval >> 21) * 125;
	if (regval & F17H_TEMP_ADJUST_MASK)
		temp -= 49000;
	return temp;
}

static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	unsigned int temp = get_raw_temp(data);

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
	unsigned int temp = get_raw_temp(data);

	return sprintf(buf, "%u\n", temp);
}

static ssize_t in1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane, vcc;

	data->read_smusvi0_tel_plane0(data->pdev, &plane);
	vcc = plane_to_vcc(plane);

	return sprintf(buf, "%d\n", vcc);
}

static ssize_t in2_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane, vcc;

	data->read_smusvi0_tel_plane1(data->pdev, &plane);
	vcc = plane_to_vcc(plane);

	return sprintf(buf, "%d\n", vcc);
}

static ssize_t curr1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_smusvi0_tel_plane0(data->pdev, &plane);
	return sprintf(buf, "%d\n", get_core_current(plane) );
}

static ssize_t curr2_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_smusvi0_tel_plane1(data->pdev, &plane);
    return sprintf(buf, "%d\n", get_soc_current(plane) );
}

static ssize_t power1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_smusvi0_tel_plane0(data->pdev, &plane);
	return sprintf(buf, "%d\n", get_core_current(plane) * plane_to_vcc(plane) );
}

static ssize_t power2_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct zenpower_data *data = dev_get_drvdata(dev);
	u32 plane;

	data->read_smusvi0_tel_plane1(data->pdev, &plane);
	return sprintf(buf, "%d\n", get_soc_current(plane) * plane_to_vcc(plane) );
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
	NULL
};

static const struct attribute_group zenpower_group = {
	.attrs = zenpower_attrs,
	.is_visible = zenpower_is_visible,
};
__ATTRIBUTE_GROUPS(zenpower);

static void read_smusvi0_tel_plane0_nb_f17(struct pci_dev *pdev, u32 *regval)
{
	amd_smn_read(amd_pci_dev_to_node_id(pdev), F17H_M01H_SVI + 0xc, regval);
}

static void read_smusvi0_tel_plane1_nb_f17(struct pci_dev *pdev, u32 *regval)
{
	amd_smn_read(amd_pci_dev_to_node_id(pdev), F17H_M01H_SVI + 0x10, regval);
}

static void read_tempreg_nb_f17(struct pci_dev *pdev, u32 *regval)
{
	amd_smn_read(amd_pci_dev_to_node_id(pdev), F17H_M01H_REPORTED_TEMP_CTRL_OFFSET, regval);
}

static int zenpower_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct zenpower_data *data;
	struct device *hwmon_dev;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	data->read_smusvi0_tel_plane0 = read_smusvi0_tel_plane0_nb_f17;
	data->read_smusvi0_tel_plane1 = read_smusvi0_tel_plane1_nb_f17;
	data->read_tempreg = read_tempreg_nb_f17;

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
	{}
};
MODULE_DEVICE_TABLE(pci, zenpower_id_table);

static struct pci_driver zenpower_driver = {
	.name = "zenpower",
	.id_table = zenpower_id_table,
	.probe = zenpower_probe,
};

module_pci_driver(zenpower_driver);
