// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/stub_cellular_networks_provider.h"

#include "base/guid.h"
#include "chromeos/network/cellular_esim_profile.h"
#include "chromeos/network/cellular_esim_profile_handler.h"
#include "chromeos/network/cellular_utils.h"
#include "chromeos/network/device_state.h"
#include "chromeos/network/network_event_log.h"

namespace chromeos {
namespace {

void GetIccids(const NetworkStateHandler::ManagedStateList& network_list,
               base::flat_set<std::string>* all_iccids,
               base::flat_set<std::string>* shill_iccids) {
  for (const std::unique_ptr<ManagedState>& managed_state : network_list) {
    const NetworkState* network = managed_state->AsNetworkState();

    // Only cellular networks have ICCIDs.
    if (!NetworkTypePattern::Cellular().MatchesType(network->type()))
      continue;

    // Skip networks that have not received any property updates yet.
    if (!network->update_received())
      continue;

    std::string iccid = network->iccid();
    if (iccid.empty()) {
      NET_LOG(ERROR) << "Cellular network missing ICCID";
      continue;
    }

    all_iccids->insert(network->iccid());

    if (!network->IsNonProfileType())
      shill_iccids->insert(network->iccid());
  }
}

}  // namespace

StubCellularNetworksProvider::StubCellularNetworksProvider() = default;

StubCellularNetworksProvider::~StubCellularNetworksProvider() {
  network_state_handler_->set_stub_cellular_networks_provider(nullptr);
}

void StubCellularNetworksProvider::Init(
    NetworkStateHandler* network_state_handler,
    CellularESimProfileHandler* cellular_esim_profile_handler) {
  network_state_handler_ = network_state_handler;
  cellular_esim_profile_handler_ = cellular_esim_profile_handler;
  network_state_handler_->set_stub_cellular_networks_provider(this);
  network_state_handler_->SyncStubCellularNetworks();
}

bool StubCellularNetworksProvider::AddOrRemoveStubCellularNetworks(
    NetworkStateHandler::ManagedStateList& network_list,
    NetworkStateHandler::ManagedStateList& new_stub_networks,
    const DeviceState* cellular_device) {
  // Do not create any new stub networks if there is no cellular device.
  if (!cellular_device)
    return false;

  base::flat_set<std::string> all_iccids, shill_iccids;
  GetIccids(network_list, &all_iccids, &shill_iccids);

  std::vector<IccidEidPair> esim_and_slot_metadata =
      GetESimAndSlotMetadata(cellular_device);

  bool network_list_changed = false;
  network_list_changed |= AddStubNetworks(
      cellular_device, esim_and_slot_metadata, all_iccids, new_stub_networks);
  network_list_changed |= RemoveStubCellularNetworks(
      esim_and_slot_metadata, shill_iccids, network_list);

  return network_list_changed;
}

bool StubCellularNetworksProvider::GetStubNetworkMetadata(
    const std::string& iccid,
    const DeviceState* cellular_device,
    std::string* service_path_out,
    std::string* guid_out) {
  std::vector<IccidEidPair> metadata_list =
      GetESimAndSlotMetadata(cellular_device);

  for (const auto& iccid_eid_pair : metadata_list) {
    if (iccid_eid_pair.first != iccid)
      continue;

    *service_path_out = GenerateStubCellularServicePath(iccid);
    *guid_out = GetGuidForStubIccid(iccid);
    return true;
  }

  return false;
}

const std::string& StubCellularNetworksProvider::GetGuidForStubIccid(
    const std::string& iccid) {
  std::string& guid = iccid_to_guid_map_[iccid];

  // If we have not yet generated a GUID for this ICCID, generate one.
  if (guid.empty())
    guid = base::GenerateGUID();

  return guid;
}

std::vector<StubCellularNetworksProvider::IccidEidPair>
StubCellularNetworksProvider::GetESimAndSlotMetadata(
    const DeviceState* cellular_device) {
  std::vector<IccidEidPair> metadata_list;

  // First, iterate through eSIM profiles and add metadata for installed
  // profiles.
  for (const auto& esim_profile :
       cellular_esim_profile_handler_->GetESimProfiles()) {
    // Skip pending and installing profiles since these are not connectable
    // networks.
    if (esim_profile.state() == CellularESimProfile::State::kInstalling ||
        esim_profile.state() == CellularESimProfile::State::kPending) {
      continue;
    }

    metadata_list.emplace_back(esim_profile.iccid(), esim_profile.eid());
  }

  // Now, iterate through SIM slots and add metadata for pSIM networks.
  for (const CellularSIMSlotInfo& sim_slot_info :
       cellular_device->sim_slot_infos()) {
    // Skip empty SIM slots.
    if (sim_slot_info.iccid.empty())
      continue;

    // Skip eSIM slots (which have associated EIDs), since these were already
    // added above.
    if (!sim_slot_info.eid.empty())
      continue;

    metadata_list.emplace_back(sim_slot_info.iccid, /*eid=*/std::string());
  }

  return metadata_list;
}

bool StubCellularNetworksProvider::AddStubNetworks(
    const DeviceState* cellular_device,
    const std::vector<IccidEidPair>& esim_and_slot_metadata,
    const base::flat_set<std::string>& all_iccids,
    NetworkStateHandler::ManagedStateList& new_stub_networks) {
  bool network_added = false;

  for (const IccidEidPair& iccid_eid_pair : esim_and_slot_metadata) {
    // Network already exists for this ICCID; no need to add a stub.
    if (base::Contains(all_iccids, iccid_eid_pair.first))
      continue;

    network_added = true;
    new_stub_networks.push_back(NetworkState::CreateNonShillCellularNetwork(
        iccid_eid_pair.first, iccid_eid_pair.second,
        GetGuidForStubIccid(iccid_eid_pair.first), cellular_device));
  }

  return network_added;
}

bool StubCellularNetworksProvider::RemoveStubCellularNetworks(
    const std::vector<IccidEidPair>& esim_and_slot_metadata,
    const base::flat_set<std::string>& shill_iccids,
    NetworkStateHandler::ManagedStateList& network_list) {
  bool network_removed = false;

  base::flat_set<std::string> esim_and_slot_iccids;
  for (const auto& iccid_eid_pair : esim_and_slot_metadata)
    esim_and_slot_iccids.insert(iccid_eid_pair.first);

  auto it = network_list.begin();
  while (it != network_list.end()) {
    const NetworkState* network = (*it)->AsNetworkState();

    // Non-Shill networks are not stubs and thus should not be removed.
    if (!network->IsNonShillCellularNetwork()) {
      ++it;
      continue;
    }

    if (shill_iccids.contains(network->iccid()) ||
        !esim_and_slot_iccids.contains(network->iccid())) {
      network_removed = true;
      it = network_list.erase(it);
      continue;
    }

    ++it;
  }

  return network_removed;
}

}  // namespace chromeos
