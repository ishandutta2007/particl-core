// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <outputtype.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/check.h>
#include <util/strencodings.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

static RPCHelpMan validateaddress()
{
    return RPCHelpMan{
        "validateaddress",
        "\nReturn information about the given particl address.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The particl address to validate"},
            {"showaltversions", RPCArg::Type::BOOL, RPCArg::Default{false}, "Display all alternative encodings and versions"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "isvalid", "If the address is valid or not"},
                {RPCResult::Type::STR, "address", /* optional */ true, "The particl address validated"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", /* optional */ true, "The hex-encoded scriptPubKey generated by the address"},
                {RPCResult::Type::BOOL, "isscript", /* optional */ true, "If the key is a script"},
                {RPCResult::Type::BOOL, "iswitness", /* optional */ true, "If the address is a witness address"},
                {RPCResult::Type::NUM, "witness_version", /* optional */ true, "The version number of the witness program"},
                {RPCResult::Type::STR_HEX, "witness_program", /* optional */ true, "The hex value of the witness program"},
                {RPCResult::Type::STR, "error", /* optional */ true, "Error message, if any"},
                {RPCResult::Type::ARR, "error_locations", /*optional=*/true, "Indices of likely error locations in address, if known (e.g. Bech32 errors)",
                    {
                        {RPCResult::Type::NUM, "index", "index of a potential error"},
                    }},
                {RPCResult::Type::STR, "stakeonly_address", /* optional */ true, "The particl address encoded as a stakeonly_address"},
                {RPCResult::Type::BOOL, "isstakeonly", /* optional */ true, "True if address is in stakeonly encoding"},
                {RPCResult::Type::STR, "bech32_address", /* optional */ true, "Alternate bech32 encoding"},
                {RPCResult::Type::STR, "base58_address", /* optional */ true, "Alternate base58 encoding"},
                {RPCResult::Type::BOOL, "isstealthaddress", /*optional=*/true, "True if the address is a stealth address."},
                {RPCResult::Type::NUM, "prefix_num_bits", /*optional=*/true, "Number of prefix bits if the address is a stealth address."},
                {RPCResult::Type::STR, "prefix_bitfield", /*optional=*/true, "Prefix if the address is a stealth address."},
                {RPCResult::Type::BOOL, "isextkey", /*optional=*/true, "True if the address is a extended address."},
                {RPCResult::Type::BOOL, "is256bit", /*optional=*/true, "True if the address is a 256bit address."},
            }
        },
        RPCExamples{
            HelpExampleCli("validateaddress", "\"" + EXAMPLE_ADDRESS[0] + "\"") +
            HelpExampleRpc("validateaddress", "\"" + EXAMPLE_ADDRESS[0] + "\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::string error_msg, s = request.params[0].get_str();
            std::vector<int> error_locations;
            bool fBech32 = bech32::Decode(s).data.size() > 0;
            CTxDestination dest = DecodeDestination(s, error_msg, &error_locations);
            bool isValid = IsValidDestination(dest);
            bool is_stake_only_version = false;
            if (!isValid) {
                dest = DecodeDestination(s, true);
                isValid = IsValidDestination(dest);
                if (isValid) {
                    is_stake_only_version = true;
                }
            }

            CHECK_NONFATAL(isValid == error_msg.empty());

            UniValue ret(UniValue::VOBJ);
            ret.pushKV("isvalid", isValid);
            if (is_stake_only_version) {
                ret.pushKV("isstakeonly", true);
            }
            if (isValid) {
                std::string currentAddress = EncodeDestination(dest, fBech32);
                ret.pushKV("address", currentAddress);

                CScript scriptPubKey = GetScriptForDestination(dest);
                ret.pushKV("scriptPubKey", HexStr(scriptPubKey));

                UniValue detail = DescribeAddress(dest);
                ret.pushKVs(detail);

                if (!request.params[1].isNull() && GetBool(request.params[1])) {
                    if (fBech32) {
                        ret.pushKV("base58_address", EncodeDestination(dest, false));
                    } else {
                        ret.pushKV("bech32_address", EncodeDestination(dest, true));
                    }
                    if (dest.index() == DI::_PKHash) {
                        ret.pushKV("stakeonly_address", EncodeDestination(dest, true, true));
                    }
                }
            } else {
                UniValue error_indices(UniValue::VARR);
                for (int i : error_locations) error_indices.push_back(i);
                ret.pushKV("error_locations", error_indices);
                ret.pushKV("error", error_msg);
            }

            return ret;
        },
    };
}

static RPCHelpMan createmultisig()
{
    return RPCHelpMan{"createmultisig",
        "\nCreates a multi-signature address with n signature of m keys required.\n"
        "It returns a json object with the address and redeemScript.\n",
        {
            {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures out of the n keys."},
            {"keys", RPCArg::Type::ARR, RPCArg::Optional::NO, "The hex-encoded public keys.",
                {
                    {"key", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "The hex-encoded public key"},
                }},
            {"address_type", RPCArg::Type::STR, RPCArg::Default{"legacy"}, "The address type to use. Options are \"legacy\", \"p2sh-segwit\", and \"bech32\"."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The value of the new multisig address."},
                {RPCResult::Type::STR_HEX, "redeemScript", "The string value of the hex-encoded redemption script."},
                {RPCResult::Type::STR, "descriptor", "The descriptor for this multisig"},
                {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Any warnings resulting from the creation of this multisig",
                {
                    {RPCResult::Type::STR, "", ""},
                }},
            }
        },
        RPCExamples{
            "\nCreate a multisig address from 2 public keys\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\\\",\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\\\"]\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("createmultisig", "2, [\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\",\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\"]")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            int required = request.params[0].getInt<int>();

            // Get the public keys
            const UniValue& keys = request.params[1].get_array();
            std::vector<CPubKey> pubkeys;
            for (unsigned int i = 0; i < keys.size(); ++i) {
                if (IsHex(keys[i].get_str()) && (keys[i].get_str().length() == 66 || keys[i].get_str().length() == 130)) {
                    pubkeys.push_back(HexToPubKey(keys[i].get_str()));
                } else {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid public key: %s\n.", keys[i].get_str()));
                }
            }

            // Get the output type
            OutputType output_type = OutputType::LEGACY;
            if (!request.params[2].isNull()) {
                std::optional<OutputType> parsed = ParseOutputType(request.params[2].get_str());
                if (!parsed) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[2].get_str()));
                } else if (parsed.value() == OutputType::BECH32M) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "createmultisig cannot create bech32m multisig addresses");
                }
                output_type = parsed.value();
            }

            // Construct using pay-to-script-hash:
            FillableSigningProvider keystore;
            CScript inner;
            const CTxDestination dest = AddAndGetMultisigDestination(required, pubkeys, output_type, keystore, inner);

            // Make the descriptor
            std::unique_ptr<Descriptor> descriptor = InferDescriptor(GetScriptForDestination(dest), keystore);

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", EncodeDestination(dest));
            result.pushKV("redeemScript", HexStr(inner));
            result.pushKV("descriptor", descriptor->ToString());

            UniValue warnings(UniValue::VARR);
            if (descriptor->GetOutputType() != output_type) {
                // Only warns if the user has explicitly chosen an address type we cannot generate
                warnings.push_back("Unable to make chosen address type, please ensure no uncompressed public keys are present.");
            }
            PushWarnings(warnings, result);

            return result;
        },
    };
}

static RPCHelpMan getdescriptorinfo()
{
    const std::string EXAMPLE_DESCRIPTOR = "wpkh([d34db33f/84h/0h/0h]0279be667ef9dcbbac55a06295Ce870b07029Bfcdb2dce28d959f2815b16f81798)";

    return RPCHelpMan{"getdescriptorinfo",
        {"\nAnalyses a descriptor.\n"},
        {
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "descriptor", "The descriptor in canonical form, without private keys"},
                {RPCResult::Type::STR, "checksum", "The checksum for the input descriptor"},
                {RPCResult::Type::BOOL, "isrange", "Whether the descriptor is ranged"},
                {RPCResult::Type::BOOL, "issolvable", "Whether the descriptor is solvable"},
                {RPCResult::Type::BOOL, "hasprivatekeys", "Whether the input descriptor contained at least one private key"},
            }
        },
        RPCExamples{
            "Analyse a descriptor\n" +
            HelpExampleCli("getdescriptorinfo", "\"" + EXAMPLE_DESCRIPTOR + "\"") +
            HelpExampleRpc("getdescriptorinfo", "\"" + EXAMPLE_DESCRIPTOR + "\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            FlatSigningProvider provider;
            std::string error;
            auto desc = Parse(request.params[0].get_str(), provider, error);
            if (!desc) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("descriptor", desc->ToString());
            result.pushKV("checksum", GetDescriptorChecksum(request.params[0].get_str()));
            result.pushKV("isrange", desc->IsRange());
            result.pushKV("issolvable", desc->IsSolvable());
            result.pushKV("hasprivatekeys", provider.keys.size() > 0);
            return result;
        },
    };
}

static RPCHelpMan deriveaddresses()
{
    const std::string EXAMPLE_DESCRIPTOR = "wpkh([d34db33f/84h/0h/0h]xpub6DJ2dNUysrn5Vt36jH2KLBT2i1auw1tTSSomg8PhqNiUtx8QX2SvC9nrHu81fT41fvDUnhMjEzQgXnQjKEu3oaqMSzhSrHMxyyoEAmUHQbY/0/*)#cjjspncu";

    return RPCHelpMan{"deriveaddresses",
        {"\nDerives one or more addresses corresponding to an output descriptor.\n"
         "Examples of output descriptors are:\n"
         "    pkh(<pubkey>)                                     P2PKH outputs for the given pubkey\n"
         "    wpkh(<pubkey>)                                    Native segwit P2PKH outputs for the given pubkey\n"
         "    sh(multi(<n>,<pubkey>,<pubkey>,...))              P2SH-multisig outputs for the given threshold and pubkeys\n"
         "    raw(<hex script>)                                 Outputs whose scriptPubKey equals the specified hex scripts\n"
         "    tr(<pubkey>,multi_a(<n>,<pubkey>,<pubkey>,...))   P2TR-multisig outputs for the given threshold and pubkeys\n"
         "\nIn the above, <pubkey> either refers to a fixed public key in hexadecimal notation, or to an xpub/xprv optionally followed by one\n"
         "or more path elements separated by \"/\", where \"h\" represents a hardened child key.\n"
         "For more information on output descriptors, see the documentation in the doc/descriptors.md file.\n"},
        {
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor."},
            {"range", RPCArg::Type::RANGE, RPCArg::Optional::OMITTED, "If a ranged descriptor is used, this specifies the end or the range (in [begin,end] notation) to derive."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::STR, "address", "the derived addresses"},
            }
        },
        RPCExamples{
            "First three native segwit receive addresses\n" +
            HelpExampleCli("deriveaddresses", "\"" + EXAMPLE_DESCRIPTOR + "\" \"[0,2]\"") +
            HelpExampleRpc("deriveaddresses", "\"" + EXAMPLE_DESCRIPTOR + "\", \"[0,2]\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const std::string desc_str = request.params[0].get_str();

            int64_t range_begin = 0;
            int64_t range_end = 0;

            if (request.params.size() >= 2 && !request.params[1].isNull()) {
                std::tie(range_begin, range_end) = ParseDescriptorRange(request.params[1]);
            }

            FlatSigningProvider key_provider;
            std::string error;
            auto desc = Parse(desc_str, key_provider, error, /* require_checksum = */ true);
            if (!desc) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
            }

            if (!desc->IsRange() && request.params.size() > 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Range should not be specified for an un-ranged descriptor");
            }

            if (desc->IsRange() && request.params.size() == 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Range must be specified for a ranged descriptor");
            }

            UniValue addresses(UniValue::VARR);

            for (int64_t i = range_begin; i <= range_end; ++i) {
                FlatSigningProvider provider;
                std::vector<CScript> scripts;
                if (!desc->Expand(i, key_provider, scripts, provider)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Cannot derive script without private keys");
                }

                for (const CScript& script : scripts) {
                    CTxDestination dest;
                    if (!ExtractDestination(script, dest)) {
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Descriptor does not have a corresponding address");
                    }

                    addresses.push_back(EncodeDestination(dest));
                }
            }

            // This should not be possible, but an assert seems overkill:
            if (addresses.empty()) {
                throw JSONRPCError(RPC_MISC_ERROR, "Unexpected empty result");
            }

            return addresses;
        },
    };
}

void RegisterOutputScriptRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"util", &validateaddress},
        {"util", &createmultisig},
        {"util", &deriveaddresses},
        {"util", &getdescriptorinfo},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
