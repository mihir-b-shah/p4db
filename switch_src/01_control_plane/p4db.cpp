#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <sstream>
#include <unistd.h>
#include <vector>

extern "C" {
#include <bf_pm/bf_pm_intf.h>
#include <bf_rt/bf_rt_common.h>
#include <bf_switchd/bf_switchd.h>
#include <traffic_mgr/traffic_mgr.h>
}

#include <bf_rt/bf_rt_info.hpp>
#include <bf_rt/bf_rt_init.hpp>
#include <bf_rt/bf_rt_learn.hpp>
#include <bf_rt/bf_rt_session.hpp>
#include <bf_rt/bf_rt_table.hpp>
#include <bf_rt/bf_rt_table_attributes.hpp>
#include <bf_rt/bf_rt_table_key.hpp>

#ifndef SDE_INSTALL
#error "Please add -DSDE_INSTALL=\"$SDE_INSTALL\" to CPPFLAGS"
#endif

#define INIT_STATUS_TCP_PORT 7777

inline void check_status(bf_status_t status) {
    if (status != BF_SUCCESS) {
	assert(false);
    }
}

/*
 * This is the placeholder for your great NOS!
 */
int app_run(bf_switchd_context_t* switchd_ctx, const char* prog_name) {
    (void)switchd_ctx;

    bf_rt_target_t dev_tgt;
    memset(&dev_tgt, 0, sizeof(dev_tgt));
    dev_tgt.dev_id = 0;
    dev_tgt.pipe_id = 3;

    /* Creating a new session */
    auto session = bfrt::BfRtSession::sessionCreate();
    if (session == nullptr) {
        /* Probably too many are open */
		assert(false && "Probably too many open sessions.");
    }

    auto& dev_mgr = bfrt::BfRtDevMgr::getInstance();

    const bfrt::BfRtInfo* bfrt_info = nullptr;
    check_status(dev_mgr.bfRtInfoGet(dev_tgt.dev_id, prog_name, &bfrt_info));

    std::vector<const bfrt::BfRtTable*> table_vec;
    check_status(bfrt_info->bfrtInfoGetTables(&table_vec));

    std::cout << "Found " << table_vec.size() << " tables\n";
    for (auto& table : table_vec) {
        std::string name;
        check_status(table->tableNameGet(&name));

        size_t size;
        check_status(table->tableSizeGet(*session, dev_tgt, &size));

        bfrt::BfRtTable::TableType table_type;
        check_status(table->tableTypeGet(&table_type));
        // std::cout << "\t" << name << " size:" << size << " type:" << static_cast<int>(table_type) << '\n';
    }

	bf_dev_id_t dev_id = 0;

    struct PortInfo {
        bf_pal_front_port_handle_t port_hdl;
        bf_port_speed_t port_speed;
    };

    std::vector<PortInfo> ports(2);
	ports[0].port_hdl.conn_id = 31;
	ports[0].port_hdl.chnl_id = 0;
	ports[0].port_speed = BF_SPEED_100G;
	ports[1].port_hdl.conn_id = 32;
	ports[1].port_hdl.chnl_id = 0;
	ports[1].port_speed = BF_SPEED_100G;

    for (auto& port_info : ports) {
        auto& port_hdl = port_info.port_hdl;
		bf_dev_port_t dev_port;
        check_status(bf_pm_port_front_panel_port_to_dev_port_get(&port_hdl, &dev_id, &dev_port));
        check_status(bf_pm_port_add(dev_id, &port_hdl, port_info.port_speed, bf_fec_type_t::BF_FEC_TYP_NONE));
        check_status(bf_pm_port_autoneg_set(dev_id, &port_hdl, bf_pm_port_autoneg_policy_e::PM_AN_FORCE_DISABLE));
        check_status(bf_pm_pltfm_front_port_ready_for_bringup(dev_id, &port_hdl, true));

        check_status(bf_pm_port_enable(dev_id, &port_hdl));
        std::cout << "conn_id=" << port_hdl.conn_id << " chnl_id=" << port_hdl.chnl_id << " --> dev_port=" << dev_port << '\n';
    }
    std::cout << "Configured " << ports.size() << " ports.\n";

    /* Run Indefinitely */
    while (true) {
        sleep(1);
    }
}


int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <prog-name>\n";
        return -1;
    }

    const char* prog_name = argv[1];

    /* Check that we are running as root */
    if (geteuid() != 0) {
        std::cerr << "ERROR: This program must be run as root\n";
        return -1;
    }

    /* Allocate switchd context */
    bf_switchd_context_t* switchd_ctx;
    if ((switchd_ctx = (bf_switchd_context_t*)calloc(1, sizeof(bf_switchd_context_t))) == nullptr) {
        std::cerr << "Cannot Allocate switchd context\n";
        return -1;
    }

	/*	Look at this config file, make sure it is ONLY POINTING TO PIPE 3 */
    std::string conf_file{SDE_INSTALL "/share/p4/targets/tofino2/"};
    conf_file += prog_name;
    conf_file += ".conf";

    /* Minimal switchd context initialization to get things going */
    switchd_ctx->install_dir = strdup(SDE_INSTALL);
    switchd_ctx->conf_file = strdup(conf_file.c_str());
    switchd_ctx->running_in_background = true;
    switchd_ctx->dev_sts_thread = true;
    switchd_ctx->dev_sts_port = INIT_STATUS_TCP_PORT;

    /* Initialize the device */
    bf_status_t status = bf_switchd_lib_init(switchd_ctx);
    if (status != BF_SUCCESS) {
        std::cerr << "ERROR: Device initialization failed: " << bf_err_str(status) << "\n";
        return -1;
    }

    /* Run the application */
    status = app_run(switchd_ctx, prog_name);

    free(switchd_ctx);

    return status;
}
