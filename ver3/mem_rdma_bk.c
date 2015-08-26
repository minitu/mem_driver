#include "mem_rdma.h"

extern void* slabs[NSLABS];
struct memory_cb *gcb;

static int memory_cma_event_handler(struct rdma_cm_id *cma_id,
		struct rdma_cm_event *event)
{
	int ret;
	struct memory_cb *cb = cma_id->context;

	printk("cma_event type %d cma_id %p (%s)\n", event->event, cma_id,
			(cma_id == cb->cm_id) ? "parent" : "child");

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		cb->state = ADDR_RESOLVED;
		ret = rdma_resolve_route(cma_id, 2000);
		if (ret) {
			printk(KERN_ERR "rdma_resolve_route error %d\n", ret);
			wake_up_interruptible(&cb->sem);
		}
		break;
		
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		cb->state = ROUTE_RESOLVED;
		wake_up_interruptible(&cb->sem);
		break;
		
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		cb->state = CONNECT_REQUEST;
		cb->child_cm_id = cma_id;
		printk("child cma %p\n", cb->child_cm_id);
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		printk("ESTABLISHED\n");
		if (!cb->server) {
			cb->state = CONNECTED;
		}
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		printk(KERN_ERR "cma event %d, error %d\n", event->event, event->status);
		cb->state = ERROR;
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		printk(KERN_ERR "DISCONNECT EVENT...\n");
		cb->state = ERROR;
		wake_up_interruptible(&cb->sem);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		printk(KERN_ERR "cma detected device removal!!!\n");
		break;

	default:
		printk(KERN_ERR "oof bad type!\n");
		wake_up_interruptible(&cb->sem);
		break;
	}

	return 0;
}

static int server_recv(struct memory_cb *cb, struct ib_wc *wc) {
	
	if (wc->byte_len != sizeof(cb->recv_buf)) {
		printk(KERN_ERR "received bogus data, size %d\n",
				wc->byte_len);
		return -1;
	}

	cb->remote_rkey = ntohl(cb->recv_buf.rkey);
	cb->remote_addr = ntohll(cb->recv_buf.buf);
	cb->remote_len = ntohl(cb->recv_buf.size);
	printk("received rkey %x addr %llx len %d from peer\n",
			cb->remote_rkey, (unsigned long long)cb->remote_addr,
			cb->remote_len);

	if (cb->state <= CONNECTED || cb->state == RDMA_WRITE_COMPLETE)
		cb->state = RDMA_READ_ADV;
	else
		cb->state = RDMA_WRITE_ADV;

	return 0;
}

static int client_recv(struct memory_cb *cb, struct ib_wc *wc) {
	if (wc->byte_len != sizeof(cb->recv_buf)) {
		printk(KERN_ERR "received bogus data, size %d\n",
				wc->byte_len);
		return -1;
	}

	if (cb->state == RDMA_READ_ADV)
		cb->state = RDMA_WRITE_ADV;
	else
		cb->state = RDMA_WRITE_COMPLETE;

	return 0;
}

static void memory_cq_event_handler(struct ib_cq *cq, void *ctx) {

	struct memory_cb *cb = ctx;
	struct ib_wc wc;
	struct ib_recv_wr *bad_wr;
	int ret;

	if (cb->state == ERROR) {
		printk(KERN_ERR "cq completion in ERROR state\n");
		return;
	}
	
	ib_req_notify_cq(cb->cq, IB_CQ_NEXT_COMP);

	while ((ret = ib_poll_cq(cb->cq, 1, &wc)) == 1) {
		if (wc.status) {
			if (wc.status == IB_WC_WR_FLUSH_ERR) {
				printk("cq flushed\n");
				continue;
			}
			else {
				printk(KERN_ERR "cq completion failed with "
						"wr_id %Lx status %d opcode %d vender_err %x\n",
						wc.wr_id, wc.status, wc.opcode, wc.vendor_err);
				goto error;
			}
		}

		switch (wc.opcode) {
			case IB_WC_SEND:
				printk("send completion\n");
				break;
			case IB_WC_RDMA_WRITE:
				printk("rdma write completion\n");
				cb->state = RDMA_WRITE_COMPLETE;
				wake_up_interruptible(&cb->sem);
				break;
			case IB_WC_RDMA_READ:
				printk("rdma read completion\n");
				cb->state = RDMA_READ_COMPLETE;
				wake_up_interruptible(&cb->sem);
				break;
			case IB_WC_RECV:
				printk("recv completion\n");
				ret = cb->server ? server_recv(cb, &wc) : client_recv(cb, &wc);
				if (ret) {
					printk(KERN_ERR "recv wc error: %d\n", ret);
					goto error;
				}
				ret = ib_post_recv(cb->qp, &cb->rq_wr, &bad_wr);
				if (ret) {
					printk(KERN_ERR "post recv error: %d\n", ret);
					goto error;
				}
				wake_up_interruptible(&cb->sem);
				break;
			default:
				printk(KERN_ERR "%s:%d unexpected opcode %d, shutting down\n",
						__func__, __LINE__, wc.opcode);
				goto error;
		}
	}
	if (ret) {
		printk(KERN_ERR "poll error %d\n", ret);
		goto error;
	}
	return;

error:
		cb->state = ERROR;
		wake_up_interruptible(&cb->sem);
}	

static int memory_accept(struct memory_cb *cb) {
	struct rdma_conn_param conn_param;
	int ret;

	printk("accepting client connection request\n");

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;

	ret = rdma_accept(cb->child_cm_id, &conn_param);
	if (ret) {
		printk(KERN_ERR "rdma_accept error: %d\n", ret);
		return ret;
	}

	/*
	wait_event_interruptible(cb->sem, cb->state >= CONNECTED);
	if (cb->state == ERROR) {
		printk(KERN_ERR "wait for CONNECTED STATE %d\n", cb->state);
		return -1;
	}
	*/

	return 0;
}

static void memory_setup_wr(struct memory_cb *cb) {

	cb->recv_sgl.addr = cb->recv_dma_addr;
	cb->recv_sgl.length = sizeof cb->recv_buf;
	cb->recv_sgl.lkey = cb->dma_mr->lkey;
	cb->rq_wr.sg_list = &cb->recv_sgl;
	cb->rq_wr.num_sge = 1;

	cb->send_sgl.addr = cb->send_dma_addr;
	cb->send_sgl.length = sizeof cb->send_buf;
	cb->send_sgl.lkey = cb->dma_mr->lkey;
	
	cb->sq_wr.opcode = IB_WR_SEND;
	cb->sq_wr.send_flags = IB_SEND_SIGNALED;
	cb->sq_wr.sg_list = &cb->send_sgl;
	cb->sq_wr.num_sge = 1;

	if (cb->server) {
		cb->rdma_sgl.addr = cb->rdma_dma_addr;
		cb->rdma_sq_wr.send_flags = IB_SEND_SIGNALED;
		cb->rdma_sq_wr.sg_list = &cb->rdma_sgl;
		cb->rdma_sq_wr.num_sge = 1;
	}
}

static int memory_setup_buffers(struct memory_cb *cb) {

	int ret;
	struct ib_phys_buf buf;
	u64 iovbase;

	printk("memory_setup_buffers called on cb %p\n", cb);

	cb->recv_dma_addr = dma_map_single(cb->pd->device->dma_device,
			&cb->recv_buf,
			sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
	pci_unmap_addr_set(cb, recv_mapping, cb->recv_dma_addr);
	cb->send_dma_addr = dma_map_single(cb->pd->device->dma_device,
			&cb->send_buf,
			sizeof(cb->send_buf), DMA_BIDIRECTIONAL);
	pci_unmap_addr_set(cb, send_mapping, cb->send_dma_addr);

	cb->dma_mr = ib_get_dma_mr(cb->pd, IB_ACCESS_LOCAL_WRITE|
			IB_ACCESS_REMOTE_READ|IB_ACCESS_REMOTE_WRITE);
	if (IS_ERR(cb->dma_mr)) {
		printk("reg_dmamr failed\n");
		ret = PTR_ERR(cb->dma_mr);
		goto bail;
	}

	cb->rdma_buf = kmalloc(cb->size, GFP_KERNEL);
	if (!cb->rdma_buf) {
		printk("rdma_buf malloc failed\n");
		ret = -ENOMEM;
		goto bail;
	}

	cb->rdma_dma_addr = dma_map_single(cb->pd->device->dma_device,
			cb->rdma_buf, cb->size, DMA_BIDIRECTIONAL);
	pci_unmap_addr_set(cb, rdma_mapping, cb->rdma_dma_addr);

	if (!cb->server) {

		cb->start_buf = slabs[0];
		//cb->start_buf = kmalloc(cb->size, GFP_KERNEL);
		if (!cb->start_buf) {
			printk("start_buf malloc failed\n");
			ret = -ENOMEM;
			goto bail;
		}

		cb->start_dma_addr = dma_map_single(cb->pd->device->dma_device,
				cb->start_buf, cb->size, DMA_BIDIRECTIONAL);
		pci_unmap_addr_set(cb, start_mapping, cb->start_dma_addr);
	}

	memory_setup_wr(cb);
	printk("allocated & registered buffers\n");
	return 0;

bail:
	if (cb->rdma_buf)
		kfree(cb->rdma_buf);
	if (cb->start_buf)
		//kfree(cb->start_buf);
	return ret;
}

static void memory_free_buffers(struct memory_cb *cb) {
	
	printk("memory_free_buffers called on cb %p\n", cb);

	if (cb->dma_mr)
		ib_dereg_mr(cb->dma_mr);

	dma_unmap_single(cb->pd->device->dma_device,
			pci_unmap_addr(cb, recv_mapping),
			sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
	dma_unmap_single(cb->pd->device->dma_device,
			pci_unmap_addr(cb, send_mapping),
			sizeof(cb->send_buf), DMA_BIDIRECTIONAL);
	dma_unmap_single(cb->pd->device->dma_device,
			pci_unmap_addr(cb, rdma_mapping),
			cb->size, DMA_BIDIRECTIONAL);
	kfree(cb->rdma_buf);
	if (cb->start_buf) {
		dma_unmap_single(cb->pd->device->dma_device,
				pci_unmap_addr(cb, start_mapping),
				cb->size, DMA_BIDIRECTIONAL);
		//kfree(cb->start_buf);
	}
}

static int memory_create_qp(struct memory_cb *cb) {

	struct ib_qp_init_attr init_attr;
	int ret;

	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_send_wr = cb->txdepth;
	init_attr.cap.max_recv_wr = 2;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = IB_QPT_RC;
	init_attr.send_cq = cb->cq;
	init_attr.recv_cq = cb->cq;
	init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;

	if (cb->server) {
		ret = rdma_create_qp(cb->child_cm_id, cb->pd, &init_attr);
		if (!ret)
			cb->qp = cb->child_cm_id->qp;
	} else {
		ret = rdma_create_qp(cb->cm_id, cb->pd, &init_attr);
		if (!ret)
			cb->qp = cb->cm_id->qp;
	}

	return ret;
}

static void memory_free_qp(struct memory_cb *cb) {

	ib_destroy_qp(cb->qp);
	ib_destroy_cq(cb->cq);
	ib_dealloc_pd(cb->pd);
}

static int memory_setup_qp(struct memory_cb *cb, struct rdma_cm_id *cm_id) {

	int ret;
	cb->pd = ib_alloc_pd(cm_id->device);
	if (IS_ERR(cb->pd)) {
		printk(KERN_ERR "ib_alloc_pd failed\n");
		return PTR_ERR(cb->pd);
	}
	printk("created pd %p\n", cb->pd);

	cb->cq = ib_create_cq(cm_id->device, memory_cq_event_handler, NULL,
			cb, cb->txdepth * 2, 0);
	if (IS_ERR(cb->cq)) {
		printk(KERN_ERR "ib_create_cq failed\n");
		ret = PTR_ERR(cb->cq);
		goto err1;
	}
	printk("create cq %p\n", cb->cq);

	ret = ib_req_notify_cq(cb->cq, IB_CQ_NEXT_COMP);
	if (ret) {
		printk(KERN_ERR "ib_create_cq failed\n");
		goto err2;
	}

	ret = memory_create_qp(cb);
	if (ret) {
		printk(KERN_ERR "memory_create_qp failed: %d\n", ret);
		goto err2;
	}
	printk("created qp %p\n", cb->qp);
	
	return 0;

err2:
	ib_destroy_cq(cb->cq);
err1:
	ib_dealloc_pd(cb->pd);

	return ret;
}

static void memory_format_send(struct memory_cb *cb, u64 buf) {

	struct memory_rdma_info *info = &cb->send_buf;
	u32 rkey;

	if (!cb->server) {
		rkey = cb->dma_mr->rkey;
		info->buf = htonll(buf);
		info->rkey = htonl(rkey);
		info->size = htonl(cb->size);
		printk("RDMA addr %llx rkey %x len %d\n",
				(unsigned long long)buf, rkey, cb->size);
	}
}

static void memory_test_server(struct memory_cb *cb) {

	struct ib_send_wr *bad_wr;
	int ret;

	while (1) {
		/* Wait for client's message */
		wait_event_interruptible(cb->sem, cb->state >= RDMA_READ_ADV);
		if (cb->state != RDMA_READ_ADV) {
			printk(KERN_ERR "wait for RDMA_READ_ADV state %d\n", cb->state);
			break;
		}

		printk("server received client's update completion message\n");

		cb->rdma_sq_wr.wr.rdma.rkey = cb->remote_rkey;
		cb->rdma_sq_wr.wr.rdma.remote_addr = cb->remote_addr;
		cb->rdma_sq_wr.sg_list->length = cb->remote_len;
		cb->rdma_sgl.lkey = cb->dma_mr->rkey;
		cb->rdma_sq_wr.next = NULL;

		/* Issue RDMA read */
		cb->rdma_sq_wr.opcode = IB_WR_RDMA_READ;

		ret = ib_post_send(cb->qp, &cb->rdma_sq_wr, &bad_wr);
		if (ret) {
			printk(KERN_ERR "post send error %d\n", ret);
			break;
		}
		cb->rdma_sq_wr.next = NULL;

		printk("server posted rdma read req\n");

		/* Wait for read completion */
		wait_event_interruptible(cb->sem, cb->state >= RDMA_READ_COMPLETE);
		if (cb->state != RDMA_READ_COMPLETE) {
			printk(KERN_ERR "wait for RDMA_READ_COMPLETE state %d\n", cb->state);
			break;
		}
		printk("server received read complete\n");

		/* DEBUG: display memory content */
		//printk("content: %lu\n", (unsigned long)*(cb->rdma_buf));

		cb->state = CONNECTED;

		/* Tell client to begin again */
		ret = ib_post_send(cb->qp, &cb->sq_wr, &bad_wr);
		if (ret) {
			printk(KERN_ERR "post send error %d\n", ret);
			break;
		}
		printk("server posted go ahead\n");
	}
}

static void fill_sockaddr(struct sockaddr_storage *sin, struct memory_cb *cb) {

	memset(sin, 0, sizeof(*sin));

	if (cb->addr_type == AF_INET) {
		struct sockaddr_in *sin4 = (struct sockaddr_in *)sin;
		sin4->sin_family = AF_INET;
		memcpy((void *)&sin4->sin_addr.s_addr, cb->addr, 4);
		sin4->sin_port = cb->port;
	}
}

static int memory_bind_server(struct memory_cb *cb) {

	struct sockaddr_storage sin;
	int ret;


	fill_sockaddr(&sin, cb);

	ret = rdma_bind_addr(cb->cm_id, (struct sockaddr *)&sin);
	if (ret) {
		printk(KERN_ERR "rdma_bind_addr error %d\n", ret);
		return ret;
	}
	printk("rdma_bind_addr successful\n");

	printk("rdma_listen\n");
	ret = rdma_listen(cb->cm_id, 3);
	if (ret) {
		printk(KERN_ERR "rdma_listen failed: %d\n", ret);
		return ret;
	}

	wait_event_interruptible(cb->sem, cb->state >= CONNECT_REQUEST);
	if (cb->state != CONNECT_REQUEST) {
		printk(KERN_ERR "wait for CONNECT_REQUEST state %d\n", cb->state);
		return -1;
	}

	return 0;
}

static void memory_run_server(struct memory_cb *cb) {
	struct ib_recv_wr *bad_wr;
	int ret;

	ret = memory_bind_server(cb);
	if (ret)
		return;

	ret = memory_setup_qp(cb, cb->child_cm_id);
	if (ret) {
		printk(KERN_ERR "setup_qp failed: %d\n", ret);
		goto err0;
	}

	ret = memory_setup_buffers(cb);
	if (ret) {
		printk(KERN_ERR "memory_setup_buffers failed: %d\n", ret);
		goto err1;
	}

	ret = ib_post_recv(cb->qp, &cb->rq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR "ib_post_recv failed: %d\n", ret);
		goto err2;
	}

	ret = memory_accept(cb);
	if (ret) {
		printk(KERN_ERR "connect error %d\n", ret);
		goto err2;
	}

	memory_test_server(cb);

	/*
	int i = 0;
	while (1) {
		if (i == 50) break;

		i++;
		ssleep(1);
	}
	*/

	rdma_disconnect(cb->child_cm_id);

err2:
	memory_free_buffers(cb);
err1:
	memory_free_qp(cb);
err0:
	rdma_destroy_id(cb->child_cm_id);
}

static void memory_test_client(struct memory_cb *cb) {

	int i = 0;
	int ret;
	struct ib_send_wr *bad_wr;
	
	while(1) {
		if (i == 5) break;

		/* Update RDMA region and let server read it */
		cb->state = RDMA_READ_ADV;

		memory_format_send(cb, cb->start_dma_addr);
		if (cb->state == ERROR) {
			printk(KERN_ERR "memory_format_send failed\n");
			break;
		}

		ret = ib_post_send(cb->qp, &cb->sq_wr, &bad_wr);
		if (ret) {
			printk(KERN_ERR "post send error %d\n", ret);
			break;
		}

		/* Wait for server to ACK */
		wait_event_interruptible(cb->sem, cb->state >= RDMA_WRITE_ADV);
		if (cb->state != RDMA_WRITE_ADV) {
			printk(KERN_ERR "wait for RDMA_WRITE_ADV state %d\n",
					cb->state);
			break;
		}

		i++;
	}
}

static int memory_connect_client(struct memory_cb *cb) {
	
	struct rdma_conn_param conn_param;
	int ret;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = 10;

	ret = rdma_connect(cb->cm_id, &conn_param);
	if (ret) {
		printk(KERN_ERR "rdma_connect error %d\n", ret);
		return ret;
	}

	wait_event_interruptible(cb->sem, cb->state >= CONNECTED);
	if (cb->state == ERROR) {
		printk(KERN_ERR "wait for CONNECTED state %d\n", cb->state);
		return -1;
	}

	printk("rdma_connect successful\n");

	return 0;
}

static int memory_bind_client(struct memory_cb *cb) {

	struct sockaddr_storage sin;
	int ret;

	fill_sockaddr(&sin, cb);

	ret = rdma_resolve_addr(cb->cm_id, NULL, (struct sockaddr *)&sin, 2000);
	if (ret) {
		printk(KERN_ERR "rdma_resolve_addr error %d\n", ret);
		return ret;
	}

	wait_event_interruptible(cb->sem, cb->state >= ROUTE_RESOLVED);
	if (cb->state != ROUTE_RESOLVED) {
		printk(KERN_ERR "addr/route resolution did not resolve: state %d\n", cb->state);
		return -EINTR;
	}

	printk("rdma_resolve_addr - rdma_resolve_route successful\n");

	return 0;
}

static void memory_run_client(struct memory_cb *cb) {
	
	struct ib_recv_wr *bad_wr;
	int ret;

	ret = memory_bind_client(cb);
	if (ret)
		return;

	ret = memory_setup_qp(cb, cb->cm_id);
	if (ret) {
		printk(KERN_ERR "setup_qp failed: %d\n", ret);
		return;
	}

	ret = memory_setup_buffers(cb);
	if (ret) {
		printk(KERN_ERR "memory_setup_buffers failed: %d\n", ret);
		goto err1;
	}

	ret = ib_post_recv(cb->qp, &cb->rq_wr, &bad_wr);
	if (ret) {
		printk(KERN_ERR "ib_post_recv failed: %d\n", ret);
		goto err2;
	}

	ret = memory_connect_client(cb);
	if (ret) {
		printk(KERN_ERR "connect error %d\n", ret);
		goto err2;
	}

	memory_test_client(cb);

	// DEBUG
	/*
	int i = 0;
	while (1) {
		if (i == 50) break;
		
		i++;
		ssleep(1);
	}
	*/

	rdma_disconnect(cb->cm_id);

err2:
	memory_free_buffers(cb);
err1:
	memory_free_qp(cb);
}

int memory_rdma_exec(char *cmd) {
	struct memory_cb *cb;
	int op;
	int ret = 0;
	char *optarg;
	unsigned long optint;

	gcb = kzalloc(sizeof(*cb), GFP_KERNEL);
	cb = gcb;

	cb->server = -1;
	cb->state = IDLE;
	cb->size = MEMORY_BUFSIZE;
	cb->txdepth = MEMORY_SQ_DEPTH;
	init_waitqueue_head(&cb->sem);

	/* Parse cmd */
	char *token, *string, *tofree;
	int i = 0;

	tofree = string = kstrdup(cmd, GFP_KERNEL);

	while ((token = strsep(&string, ",")) != NULL) {
		if (i == 0) { // server or client
			if (strcmp(token, "server") == 0) {
				cb->server = 1;
				printk("server\n");
			}
			else if (strcmp(token, "client") == 0) {
				cb->server = 0;
				printk("client\n");
			}
		}
		else if (i == 1) { // server's IP addr
			cb->addr_str = kstrdup(token, GFP_KERNEL);
			in4_pton(token, -1, cb->addr, -1, NULL);
			cb->addr_type = AF_INET;
			printk("ipaddr (%s)\n", token);
		}
		else if (i == 2) { // port
			int _port;
			kstrtoint(token, 0, &_port);
			cb->port = htons(_port);
			printk("port %d\n", _port);
		}
		else {
			printk("wrong command\n");
			ret = -EINVAL;
		}

		i++;
	}

	kfree(tofree);

	if (ret)
		goto out;

	if (cb->server == -1) {
		printk(KERN_ERR "must be either client or server\n");
		ret = -EINVAL;
		goto out;
	}

	/* RDMA */
	cb->cm_id = rdma_create_id(memory_cma_event_handler, cb, RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cb->cm_id)) {
		ret = PTR_ERR(cb->cm_id);
		printk(KERN_ERR "rdma_create_id error %d\n", ret);
		goto out;
	}
	printk("created cm_id %p\n", cb->cm_id);

	if (cb->server)
		memory_run_server(cb);
	else
		memory_run_client(cb);

	printk("destroy cm_id %p\n", cb->cm_id);
	rdma_destroy_id(cb->cm_id);

out:
	kfree(cb);
	return ret;
}
