enum rru_device_id{
	IPCORE_LEDCTL,
	IPCORE_CAPCTL,
	IPCORE_FPGAINFO
};

enum plip_ioc_command{
	RRU_IOC_REG_READ = 100,
	RRU_IOC_REG_WRITE,
};

typedef struct{
	unsigned addr;
	unsigned value;
	char id;
} plip_iocReg_t;