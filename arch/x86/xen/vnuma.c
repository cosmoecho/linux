#include <linux/err.h>
#include <linux/memblock.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <asm/xen/interface.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/vnuma.h>

#ifdef CONFIG_NUMA

/* Checks if hypercall is supported */
bool xen_vnuma_supported(void)
{
	return HYPERVISOR_memory_op(XENMEM_get_vnuma_info, NULL)
					== -ENOSYS ? false : true;
}

/*
 * Called from numa_init if numa_off = 0;
 * we set numa_off = 0 if xen_vnuma_supported()
 * returns true and its a domU;
 */
int __init xen_numa_init(void)
{
	int rc;
	unsigned int i, j, nr_nodes, cpu, idx, pcpus;
	u64 physm, physd, physc;
	unsigned int *vdistance, *cpu_to_node;
	unsigned long mem_size, dist_size, cpu_to_node_size;
	struct vmemrange *vblock;

	struct vnuma_topology_info numa_topo = {
		.domid = DOMID_SELF,
		.__pad = 0
	};
	rc = -EINVAL;
	physm = physd = physc = 0;

	/* For now only PV guests are supported */
	if (!xen_pv_domain())
		return rc;

	pcpus = num_possible_cpus();

	mem_size =  pcpus * sizeof(struct vmemrange);
	dist_size = pcpus * pcpus * sizeof(*numa_topo.distance);
	cpu_to_node_size = pcpus * sizeof(*numa_topo.cpu_to_node);

	physm = memblock_alloc(mem_size, PAGE_SIZE);
	vblock = __va(physm);

	physd = memblock_alloc(dist_size, PAGE_SIZE);
	vdistance  = __va(physd);

	physc = memblock_alloc(cpu_to_node_size, PAGE_SIZE);
	cpu_to_node  = __va(physc);

	if (!physm || !physc || !physd)
		goto out;

	set_xen_guest_handle(numa_topo.nr_nodes, &nr_nodes);
	set_xen_guest_handle(numa_topo.memrange, vblock);
	set_xen_guest_handle(numa_topo.distance, vdistance);
	set_xen_guest_handle(numa_topo.cpu_to_node, cpu_to_node);

	rc = HYPERVISOR_memory_op(XENMEM_get_vnuma_info, &numa_topo);

	if (rc < 0)
		goto out;
	nr_nodes = *numa_topo.nr_nodes;
	if (nr_nodes == 0)
		goto out;
	if (nr_nodes > num_possible_cpus()) {
		pr_debug("vNUMA: Node without cpu is not supported in this version.\n");
		goto out;
	}

	/*
	 * NUMA nodes memory ranges are in pfns, constructed and
	 * aligned based on e820 ram domain map.
	 */
	for (i = 0; i < nr_nodes; i++) {
		if (numa_add_memblk(i, vblock[i].start, vblock[i].end))
			goto out;
		node_set(i, numa_nodes_parsed);
	}

	setup_nr_node_ids();
	/* Setting the cpu, apicid to node */
	for_each_cpu(cpu, cpu_possible_mask) {
		set_apicid_to_node(cpu, cpu_to_node[cpu]);
		numa_set_node(cpu, cpu_to_node[cpu]);
		cpumask_set_cpu(cpu, node_to_cpumask_map[cpu_to_node[cpu]]);
	}

	for (i = 0; i < nr_nodes; i++) {
		for (j = 0; j < *numa_topo.nr_nodes; j++) {
			idx = (j * nr_nodes) + i;
			numa_set_distance(i, j, *(vdistance + idx));
		}
	}

	rc = 0;
out:
	if (physm)
		memblock_free(__pa(physm), mem_size);
	if (physd)
		memblock_free(__pa(physd), dist_size);
	if (physc)
		memblock_free(__pa(physc), cpu_to_node_size);
	/*
	 * Set a dummy node and return success.  This prevents calling any
	 * hardware-specific initializers which do not work in a PV guest.
	 * Taken from dummy_numa_init code.
	 */
	if (rc != 0) {
		for (i = 0; i < MAX_LOCAL_APIC; i++)
			set_apicid_to_node(i, NUMA_NO_NODE);
		nodes_clear(numa_nodes_parsed);
		nodes_clear(node_possible_map);
		nodes_clear(node_online_map);
		node_set(0, numa_nodes_parsed);
		numa_add_memblk(0, 0, PFN_PHYS(max_pfn));
	}
	return 0;
}
#endif
