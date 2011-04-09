/*
//#include <pcap/pcap.h>
*/

#include <stdio.h>

#include "natopen.hh"
#include "socket.hh"
//#include "wlan.hh"

#undef NDEBUG
#include <assert.h>


void test_hashtable() {
  typedef HashTable< HashAdapter<int> > HashInt;
  {
    HashInt h;
    assert(h.size() == 0);
    assert((int)HashAdapter<int>() == 0);

    h[3] = 3;
    h[4] = 4;
    assert(h.size() == 2);
    assert(h[3] == 3);
    assert((int)((const HashInt &)h)[5] == 0);
    assert(h[5] == 5); // auto-include, expected behavior
    assert(h.size() == 3);
    assert(h.find(5).live());
    assert(!h.find(6).live());
    HashInt::iterator i = h.find_insert(7);
    assert(i->u == 7);
    assert(*i == 7);
    assert(h.size() == 4);
    unsigned int count = 0;
    for(HashInt::const_iterator ci = h.begin(); ci.live(); ++ci, ++count);
    assert(h.size() == count);
    // test == end()
    for(HashInt::const_iterator ci = h.begin(); ci != h.end(); ++ci, --count);
    assert(count == 0);
    i = h.find(3);
    i = h.erase(i);
    assert(h.size() == 3);
    assert(!h.find(3).live());
    h.erase(4);
    assert(h.size() == 2);
    // growth test
    for (int x = 100; x < 200; ++x) h[x];
    for (int x = 100; x < 200; ++x) assert(h.find(x).live());
    assert(h.size() == 102);
  }
}

void test_hashmap() {
  {
    HashMap<int, int> h;
    h[3] = 30;
    h[4] = 40;
    assert(h[3] == 30);
    assert(h[4] == 40);
    assert(h[5] == 0);
    assert(h.size() == 3);
  }
}

void test_queue() {
  {
    Queue<> q(16);
    assert(q.size() == 0);
    assert(q.empty());
    *(int *)q.tail().data() = 42;
    q.tail().put(sizeof(int));
    q.pushTail();
    assert(q.size() == 1);
    assert(!q.empty());
    assert(q.head().size() == sizeof(int));
    assert(*(int *)q.head().data() == 42);
    q.popHead();
    assert(q.empty());
    for (int i = 0; i < 16; ++i) q.pushTail();
    assert(q.full());
    for (int i = 0; i < 16; ++i) q.popHead();
    assert(q.empty());
  }
}

/*
void test_wlan() {
  {
    IfCtl wc("tiwlan0");
    IfCtl::Params p("szymtest", (uint8_t *)"\x01\x22\x22\x22\x22\x22", 1);
    assert(wc.setAddress(inet_addr("192.168.5.1"), inet_addr("255.255.255.0")));
    assert(wc.setOperState(true));
    assert(wc.associate(p));
  }
}
*/

void test_socket() {
  {
    Buffer b;
    PacketSocket s;
    assert(s.fd() >= 0);
    assert(s.bind("eth1"));
    Selector sel;
    sel.newFd(s.fd());
    for (int i = 0; i < 10; ++i) { // wait for 10 packets
      sel.wantRead(s.fd(), true);
      printf("selecting\n");
      assert(sel.select() == 1);
      assert(sel.canRead(s.fd()));
      sel.wantRead(s.fd(), false);
      assert(s.recv(b));
      printf("got packet %d bytes\n", b.size());
      sel.wantWrite(s.fd(), true);
      printf("selecting\n");
      assert(sel.select() == 1);
      assert(sel.canWrite(s.fd()));
      sel.wantWrite(s.fd(), false);
      //if (!s.send(b))
      //  perror("couldn't send");
    }
  }
}

void test_ipsocket() {
  {
    Buffer b;
    IPSocket s;
    assert(s.fd());
    assert(s.bind());
    sleep(1000);
    //while(s.recv(b)) {
    //  printf("recv %d\n", b.size());
    //}
    perror("Done receiving? ");
  }
}

void test_ipflow() {
  {
//     NEW (17| 169.254.5.210:5353 > 224.0.0.251:5353) ==> 32000
//     NEW (17| 169.254.5.210:33419 > 224.0.0.251:50149) ==> 32001

    uint8_t proto = IPPROTO_UDP;
    in_addr_t src = inet_addr("169.254.5.210");
    in_addr_t dst = inet_addr("224.0.0.251");
    in_addr_t newsrc = inet_addr("10.237.165.188");
    uint16_t srcport = htons(5353);
    uint16_t dstport = htons(5353);
    //uint16_t newport = htons(32001);

    Buffer b;
    iphdr *ip = (iphdr *)b.data();
    ip->saddr = src;
    ip->daddr = dst;
    ip->protocol = proto;
    ip->ihl = 5;
    udphdr *udp = (udphdr *)transport_header(b);
    udp->source = srcport;
    udp->dest = dstport;

    Rewriter::Config c;
    c.out_addr = newsrc;
    c.netmask = inet_addr("255.255.255.0");
    c.subnet = inet_addr("192.168.5.0");
    c.numpreserved = 0;
    c.preserved = 0;
    c.numports = 100;
    c.firstport = 32000;
    c.log = true;

    Rewriter rw(c);

    rw.packetOut(b);

    ip->saddr = src;
    ip->daddr = dst;
    ip->protocol = proto;
    udp->source = htons(33419);
    udp->dest = htons(50149);

    rw.packetOut(b);
/*

    IPFlowId before(b);
    IPFlowId _id(src, newsrc, srcport, newport, proto);
    IPFlowId after(_id.daddr, before.daddr, _id.dport, before.dport, before.protocol);
    Translation(before, after).apply(b);
#define P(id) printf("%20s\t%s\n", #id, unparse(id))
    P(_id);
    P(before);
    P(after);
    P(IPFlowId(b));
    P(IPFlowId(b).reverse());
    assert(IPFlowIdIn(IPFlowId(b).reverse()) == (IPFlowIdIn)_id);
*/
  }
}

void test_nat() {
  { // without cleanup()
    // use two back-to-back rewriters

    Rewriter::Config c;
    c.out_addr = inet_addr("1.0.0.1");
    c.netmask = inet_addr("255.255.255.0");
    c.subnet = c.netmask & c.out_addr;
    c.numpreserved = 0;
    c.preserved = 0;
    c.numports = 100;
    c.firstport = 32000;
    c.log = true;

    Rewriter rw(c);
    Buffer b;
/*
    char errbuf[PCAP_ERRBUF_SIZE];
    // do the nat on a test tcpdump
    pcap_t *pin = pcap_open_offline("-", errbuf) // open savefile for reading
    pcap_t *pout = pcap_open_dead(DLT_RAW, b.room());
    pcap_dumper_t *pdump = pcap_dump_open(pout, "-") // open savefile for writing

    pcap_pkthdr *hdr;
    u_char *data;

    while(pcap_next_ex(pin, hdr, data) == 1) {
      memcpy(b.data(), data, hdr->caplen);
      b.put(hdr->caplen);
      // determine direction, in or out
      // if packet is coming from our host
      //   rw1.packetOut

      if (rw1.packetIn(b)) {
        if (rw2.packetIn(b)) {
          pcap_dump((u_char *)pdump, hdr, b.data());
        }
      }
    }
    pcap_dump_flush(pdump);
    pcap_dump_close(pdump);
*/
  }
}

int main(/*int argc, const char * argv[]*/) {
  test_hashtable();
  test_hashmap();
  test_queue();
  //test_wlan();
  //test_socket();
  //test_ipsocket();
  test_ipflow();
  assert(0); // testing if assert works
  return 0;
}
