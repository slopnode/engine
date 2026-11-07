#pragma once

#include "core/package.hpp"

#include <s7.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

class AssetStore;

enum class NetTypeKind {
    Bool,
    Int,
    Real,
    Float,
    Symbol,
    String,
    Entity,
    List,
    Table,
};

struct NetType {
    NetTypeKind kind = NetTypeKind::Bool;
    std::int64_t lo = 0;
    std::int64_t hi = 0;
    double realLo = 0.0;
    double realHi = 0.0;
    double step = 0.0;
    std::vector<std::string> symbols;
    int maxCount = 0;
    std::vector<NetType> items;
};

struct NetValue {
    NetTypeKind kind = NetTypeKind::Bool;
    bool flag = false;
    std::int64_t integer = 0;
    double real = 0.0;
    std::string text;
    std::vector<NetValue> items;

    bool operator==(const NetValue&) const = default;
};

enum class NetScopeKind {
    Player,
    Session,
    Thing,
};

struct NetScope {
    NetScopeKind kind = NetScopeKind::Player;
    std::string tag;

    bool operator==(const NetScope&) const = default;
};

enum class NetAudience {
    All,
    Owner,
    None,
};

struct NetField {
    std::string name;
    std::string packageId;
    PackageRole role = PackageRole::Base;
    NetScope scope;
    NetType type;
    NetValue defaultValue;
    NetAudience send = NetAudience::All;
    bool travel = false;
    bool save = false;
    int id = -1;
    int slot = -1;
};

enum class NetEventAudience {
    All,
    Owner,
    Near,
    Pvs,
};

struct NetEventArg {
    std::string name;
    NetType type;
};

struct NetEventDef {
    std::string name;
    std::string packageId;
    PackageRole role = PackageRole::Base;
    std::vector<NetEventArg> args;
    NetEventAudience to = NetEventAudience::All;
    float nearRadius = 0.0f;
    bool reliable = false;
    int id = -1;
};

struct NetBudget {
    int fields = 0;
    int bytes = 0;
};

class NetSchema {
public:
    void clear();
    void setExtension(s7_scheme* sc, s7_pointer extension);
    bool addPackageFields(
        s7_scheme* sc,
        s7_pointer state,
        std::string_view packageId,
        PackageRole role,
        std::vector<std::string>& messages);
    bool addPackageEvents(
        s7_scheme* sc,
        s7_pointer events,
        std::string_view packageId,
        PackageRole role,
        std::vector<std::string>& messages);
    void finalize();

    const std::vector<NetField>& fields() const { return fields_; }
    const NetField* find(const NetScope& scope, std::string_view name) const;
    std::vector<const NetField*> fieldsIn(const NetScope& scope) const;
    const std::vector<NetEventDef>& events() const { return events_; }
    const NetEventDef* findEvent(std::string_view name) const;
    std::uint64_t hash() const { return hash_; }

private:
    const NetBudget* budgetFor(NetScopeKind kind) const;
    NetBudget* usedFor(NetScopeKind kind);

    std::vector<NetField> fields_;
    std::vector<NetEventDef> events_;
    NetBudget eventBudget_{};
    NetBudget eventUsed_{};
    NetBudget playerBudget_{};
    NetBudget sessionBudget_{};
    NetBudget thingBudget_{};
    NetBudget playerUsed_{};
    NetBudget sessionUsed_{};
    NetBudget thingUsed_{};
    std::uint64_t hash_ = 0;
};

NetSchema& netSchema();

class NetGcPause {
public:
    explicit NetGcPause(s7_scheme* sc);
    ~NetGcPause();

    NetGcPause(const NetGcPause&) = delete;
    NetGcPause& operator=(const NetGcPause&) = delete;

private:
    s7_scheme* sc_;
};

bool parseNetType(s7_scheme* sc, s7_pointer spec, NetType& out, std::string& error);
int netTypeBits(const NetType& type);
std::string netTypeText(const NetType& type);
std::string netScopeText(const NetScope& scope);
NetValue netZeroValue(const NetType& type);
bool netValueFromScheme(s7_scheme* sc, s7_pointer value, const NetType& type, NetValue& out, std::string& error);
s7_pointer netValueToScheme(s7_scheme* sc, const NetValue& value);
void loadPackageNetSchema(s7_scheme* sc, AssetStore& assets);

}
