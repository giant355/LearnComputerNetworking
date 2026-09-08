#include "tiny_arp.hh"

#include <utility>

namespace tinyarp {

NetworkInterface::NetworkInterface(IpAddress ip, MacAddress mac)
    : ip_(std::move(ip))
    , mac_(std::move(mac))
{}

bool NetworkInterface::cache_has_live_entry(const IpAddress& ip) const
{
    const auto found = arp_cache_.find(ip);
    return found != arp_cache_.end() && found->second.expires_at_ms > now_ms_;
}

void NetworkInterface::send_ipv4_frame(
    Datagram datagram,
    const MacAddress& destination_mac
)
{
    frames_to_send_.push_back(EthernetFrame {
        .destination = destination_mac,
        .source = mac_,
        .type = FrameType::ipv4,
        .datagram = std::move(datagram),
        .arp = std::nullopt,
        .fcs_ok = true,
    });
}

void NetworkInterface::send_arp_request(const IpAddress& target_ip)
{
    frames_to_send_.push_back(EthernetFrame {
        .destination = broadcast_mac,
        .source = mac_,
        .type = FrameType::arp_request,
        .datagram = std::nullopt,
        .arp = ArpMessage {
            .sender_ip = ip_,
            .sender_mac = mac_,
            .target_ip = target_ip,
        },
        .fcs_ok = true,
    });
}

void NetworkInterface::learn_arp_mapping(const IpAddress& ip, const MacAddress& mac)
{
    arp_cache_[ip] = CacheEntry {
        .mac = mac,
        .expires_at_ms = now_ms_ + cache_lifetime_ms,
    };
}

void NetworkInterface::flush_waiting_datagrams(const IpAddress& next_hop)
{
    const auto waiting = waiting_.find(next_hop);
    const auto cache = arp_cache_.find(next_hop);

    if (waiting == waiting_.end() || cache == arp_cache_.end()) {
        return;
    }

    while (!waiting->second.empty()) {
        Datagram datagram = std::move(waiting->second.front());
        waiting->second.pop_front();
        send_ipv4_frame(std::move(datagram), cache->second.mac);
    }

    waiting_.erase(waiting);
}

void NetworkInterface::send_datagram(Datagram datagram, const IpAddress& next_hop)
{
    //查看arp缓存，进入缓存命中分支
    if (cache_has_live_entry(next_hop))
    {
        auto entry = arp_cache_.find(next_hop);
        //发送ipv4帧
        send_ipv4_frame(datagram, entry->second.mac);
    }
    else
    {
        //没命中缓存，加入待解系队列
        waiting_[next_hop].push_back(std::move(datagram));
        auto it = last_arp_request_ms_.find(next_hop);
        //再次发送请求条件：arp留存时间超时或者在一定间隔后
        if (it == last_arp_request_ms_.end() || now_ms_ - it->second >= request_suppression_ms)
        {
            send_arp_request(next_hop);
            last_arp_request_ms_[next_hop] = now_ms_;
        }
    }
}

std::optional<Datagram> NetworkInterface::recv_frame(const EthernetFrame& frame)
{
    //接收帧有两种类型
    //ethernet头里面的目标mac是询问对象，arp里面的目标mac才是我们真正想找到答案的对象
    //mac广播检查应该更加底层，不过这个lab忽略了
    if (frame.destination != mac_ && frame.destination != broadcast_mac) return std::nullopt;
    if (!frame.fcs_ok) return std::nullopt;
    if (frame.type == FrameType::ipv4 && frame.datagram)
    {
        return frame.datagram;
    }
    //只要是arp类型，不管是请求还是回复，都学习，上面ipv4不学习是因为这是主机或路由器网络接口
    else if ((frame.type == FrameType::arp_request || frame.type == FrameType::arp_reply))
    {
        learn_arp_mapping(frame.arp->sender_ip,frame.arp->sender_mac);
        flush_waiting_datagrams(frame.arp->sender_ip);
        if(frame.arp->target_ip==ip_&&frame.type==FrameType::arp_request)
        frames_to_send_.push_back(EthernetFrame{frame.arp->sender_mac,mac_,FrameType::arp_reply,std::nullopt,ArpMessage{
            .sender_ip = ip_,
            .sender_mac = mac_,
            .target_ip = frame.arp->sender_ip,
        },
        true });

    }
    return std::nullopt;
}

void NetworkInterface::tick(const std::uint64_t elapsed_ms)
{
    now_ms_ += elapsed_ms;
    for (auto it = arp_cache_.begin(); it != arp_cache_.end();) {
        if (it->second.expires_at_ms <= now_ms_) {
            it = arp_cache_.erase(it);
        }
        else {
            ++it;
        }
    }
}

std::vector<EthernetFrame> NetworkInterface::take_frames_to_send()
{
    std::vector<EthernetFrame> result;
    result.reserve(frames_to_send_.size());

    while (!frames_to_send_.empty()) {
        result.push_back(std::move(frames_to_send_.front()));
        frames_to_send_.pop_front();
    }

    return result;
}

std::optional<MacAddress> NetworkInterface::cached_mac(const IpAddress& ip) const
{
    const auto found = arp_cache_.find(ip);
    if (found == arp_cache_.end() || found->second.expires_at_ms <= now_ms_) {
        return std::nullopt;
    }

    return found->second.mac;
}

std::size_t NetworkInterface::waiting_count(const IpAddress& next_hop) const
{
    const auto found = waiting_.find(next_hop);
    return found == waiting_.end() ? 0 : found->second.size();
}

} // namespace tinyarp
