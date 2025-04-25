from scapy.all import Ether, Raw, sendp

sendp(Ether(dst='52:54:00:12:34:56')/Raw(load=input()), iface='vm0')
