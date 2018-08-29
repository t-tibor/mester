#include <stdint>



class icap_source
{
protected:
	int64_t offset;
public:
	icap_source();
	virtual int get_ts(uint64_t *buffer, uint32_t size);
	virtual int get_ovf();
}


class hw_icap_source: public icap_source
{
protected:
	char *fname;
	int fileno;
public:
	hw_icap_source();
	~hw_icap_source();


}


class virtual_icap_src: public icap_source
{
protected:
	uint64_t next_val;
	uint64_t period;
public:
	virtual_icap_src();
	~virtual_icap_src();
}



class 