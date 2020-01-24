#include "user_administration.hpp"

#undef rxGeneralAdmin

#ifdef IRODS_USER_ADMINISTRATION_ENABLE_SERVER_SIDE_API
    #include "rsGeneralAdmin.hpp"
    #define rxGeneralAdmin rsGeneralAdmin
    #define RODS_SERVER 1 // For irods::query<rsComm_t>
#else
    #include "generalAdmin.h"
    #define rxGeneralAdmin rcGeneralAdmin
#endif // IRODS_USER_ADMINISTRATION_ENABLE_SERVER_SIDE_API

#include "obf.h"
#include "authenticate.h"
#include "irods_query.hpp"

#include "json.hpp"

#include <array>
#include <iostream>

using json = nlohmann::json;

namespace irods::experimental::administration
{
    user::user(std::string name, std::optional<std::string> zone)
        : name{std::move(name)}
        , zone{zone ? *zone : ""}
    {
    }

    group::group(std::string name)
        : name{std::move(name)}
    {
    }
}

namespace irods::experimental::administration::NAMESPACE_IMPL
{
    namespace
    {
        auto to_user_type(std::string_view user_type_string) -> user_type
        {
            // clang-format off
            if      (user_type_string == "rodsuser")  { return user_type::rodsuser; }
            else if (user_type_string == "rodsadmin") { return user_type::rodsadmin; }
            // clang-format on

            throw user_management_error{"undefined user type"};
        }

        auto to_zone_type(std::string_view zone_type_string) -> zone_type
        {
            // clang-format off
            if      (zone_type_string == "local")  { return zone_type::local; }
            else if (zone_type_string == "remote") { return zone_type::remote; }
            // clang-format on

            throw user_management_error{"undefined zone type"};
        }

        auto to_c_str(user_type user_type) -> const char*
        {
            // clang-format off
            switch (user_type) {
                case user_type::rodsuser:  return "rodsuser";
                case user_type::rodsadmin: return "rodsadmin";
                default:                   break;
            }
            // clang-format on

            throw user_management_error{"cannot convert user_type to string"};
        }

        auto get_local_zone(rxComm& conn) -> std::string
        {
#ifdef IRODS_USER_ADMINISTRATION_ENABLE_SERVER_SIDE_API
            return getLocalZoneName();
#else
            for (auto&& row : irods::query{&conn, "select ZONE_NAME where ZONE_TYPE = 'local'"}) {
                return row[0];
            }

            throw user_management_error{"cannot get local zone name"};
#endif // IRODS_USER_ADMINISTRATION_ENABLE_SERVER_SIDE_API
        }

        auto obfuscate_password(std::string_view new_password) -> std::string
        {
            std::array<char, MAX_PASSWORD_LEN + 10> plain_text_password{};
            std::strncpy(plain_text_password.data(), new_password.data(), MAX_PASSWORD_LEN);

            if (const auto lcopy = MAX_PASSWORD_LEN - 10 - new_password.size(); lcopy > 15) {
                // The random string (second argument) is used to pad and must match the 
                // what is defined on the server-side.
                std::strncat(plain_text_password.data(), "1gCBizHWbwIYyWLoysGzTe6SyzqFKMniZX05faZHWAwQKXf6Fs", lcopy);
            }

            std::array<char, MAX_PASSWORD_LEN + 10> key{};

            // Decode (or prompt for current password) and store the obfuscated password in key.
            // "obfGetPw" decodes the obfuscated password stored in .irods/.irodsA.
            if (obfGetPw(key.data()) != 0) {
                throw user_management_error{"password obfuscation failed"};
            }

            std::array<char, MAX_PASSWORD_LEN + 100> obfuscate_password{};
            obfEncodeByKey(plain_text_password.data(), key.data(), obfuscate_password.data());

            return obfuscate_password.data();
        }
    } // anonymous namespace

    auto make_user(rxComm& conn,
                   const user& user,
                   user_type user_type,
                   zone_type zone_type) -> std::error_code
    {
        std::string name = unique_name(conn, user);
        std::string zone;

        if (zone_type::local == zone_type) {
            zone = get_local_zone(conn);
        }

        generalAdminInp_t input{};
        input.arg0 = "add";
        input.arg1 = "user";
        input.arg2 = name.data();
        input.arg3 = to_c_str(user_type);
        input.arg4 = zone.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto remove_user(rxComm& conn, const user& user) -> std::error_code
    {
        const auto name = unique_name(conn, user);

        generalAdminInp_t input{};
        input.arg0 = "rm";
        input.arg1 = "user";
        input.arg2 = name.data();
        input.arg3 = user.zone.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto set_user_password(rxComm& conn, const user& user, std::string_view new_password) -> std::error_code
    {
        const auto obfuscated_password = obfuscate_password(new_password);

        generalAdminInp_t input{};
        input.arg0 = "modify";
        input.arg1 = "user";
        input.arg2 = user.name.data();
        input.arg3 = "password";
        input.arg4 = obfuscated_password.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto set_user_type(rxComm& conn, const user& user, user_type new_user_type) -> std::error_code
    {
        const auto name = unique_name(conn, user);

        generalAdminInp_t input{};
        input.arg0 = "modify";
        input.arg1 = "user";
        input.arg2 = name.data();
        input.arg3 = "type";
        input.arg4 = to_c_str(new_user_type);

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto add_user_auth(rxComm& conn, const user& user, std::string_view auth) -> std::error_code
    {
        const auto name = unique_name(conn, user);

        generalAdminInp_t input{};
        input.arg0 = "modify";
        input.arg1 = "user";
        input.arg2 = name.data();
        input.arg3 = "addAuth";
        input.arg4 = auth.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto remove_user_auth(rxComm& conn, const user& user, std::string_view auth) -> std::error_code
    {
        const auto name = unique_name(conn, user);

        generalAdminInp_t input{};
        input.arg0 = "modify";
        input.arg1 = "user";
        input.arg2 = name.data();
        input.arg3 = "rmAuth";
        input.arg4 = auth.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    // Group Management

    auto make_group(rxComm& conn, const group& group) -> std::error_code
    {
        const auto zone = get_local_zone(conn);

        generalAdminInp_t input{};
        input.arg0 = "add";
        input.arg1 = "user";
        input.arg2 = group.name.data();
        input.arg3 = "rodsgroup";
        input.arg4 = zone.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto remove_group(rxComm& conn, const group& group) -> std::error_code
    {
        const auto zone = get_local_zone(conn);

        generalAdminInp_t input{};
        input.arg0 = "rm";
        input.arg1 = "user";
        input.arg2 = group.name.data();
        input.arg3 = zone.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto add_user_to_group(rxComm& conn, const group& group, const user& user) -> std::error_code
    {
        generalAdminInp_t input{};
        input.arg0 = "modify";
        input.arg1 = "group";
        input.arg2 = group.name.data();
        input.arg3 = "add";
        input.arg4 = user.name.data();
        input.arg5 = user.zone.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    auto remove_user_from_group(rxComm& conn, const group& group, const user& user) -> std::error_code
    {
        generalAdminInp_t input{};
        input.arg0 = "modify";
        input.arg1 = "group";
        input.arg2 = group.name.data();
        input.arg3 = "remove";
        input.arg4 = user.name.data();
        input.arg5 = user.zone.data();

        if (const auto ec = rxGeneralAdmin(&conn, &input); ec != 0) {
            return std::error_code{ec, std::generic_category()};
        }

        return {};
    }

    // Query

    auto users(rxComm& conn) -> std::vector<user>
    {
        std::vector<user> users;

        for (auto&& row : irods::query{&conn, "select USER_NAME, USER_ZONE where USER_TYPE != 'rodsgroup'"}) {
            users.emplace_back(row[0], row[1]);
        }

        return users;
    }

    auto users(rxComm& conn, const group& group) -> std::vector<user>
    {
        std::vector<user> users;

        std::string gql = "select USER_NAME, USER_ZONE "
                          "where USER_TYPE != 'rodsgroup' and USER_GROUP_NAME = '";
        gql += group.name;
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            users.emplace_back(row[0], row[1]);
        }

        return users;
    }

    auto groups(rxComm& conn) -> std::vector<group>
    {
        std::vector<group> groups;

        // Some groups will not show up because of the condition.
        // (e.g. "public" will not show up via this query).
        for (auto&& row : irods::query{&conn, "select USER_GROUP_NAME where USER_TYPE = 'rodsgroup'"}) {
            groups.emplace_back(row[0]);
        }

        return groups;
    }

    auto groups(rxComm& conn, const user& user) -> std::vector<group>
    {
        std::vector<group> groups;

        std::string gql = "select USER_GROUP_NAME where USER_TYPE = 'rodsgroup' and USER_NAME = '";
        gql += unique_name(conn, user);
        gql += "' and USER_ZONE = '";
        gql += (user.zone.empty() ? get_local_zone(conn) : user.zone);
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            groups.emplace_back(row[0]);
        }

        return groups;
    }

    auto exists(rxComm& conn, const user& user) -> bool
    {
        std::string gql = "select USER_ID where USER_TYPE != 'rodsgroup' and USER_NAME = '";
        gql += unique_name(conn, user);
        gql += "' and USER_ZONE = '";
        gql += (user.zone.empty() ? get_local_zone(conn) : user.zone);
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            static_cast<void>(row);
            return true;
        }

        return false;
    }

    auto exists(rxComm& conn, const group& group) -> bool
    {
        std::string gql = "select USER_GROUP_ID where USER_TYPE = 'rodsgroup' and USER_GROUP_NAME = '";
        gql += group.name;
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            static_cast<void>(row);
            return true;
        }

        return false;
    }

    auto id(rxComm& conn, const user& user) -> std::optional<std::string>
    {
        std::string gql = "select USER_ID where USER_TYPE != 'rodsgroup' and USER_NAME = '";
        gql += unique_name(conn, user);
        gql += "' and USER_ZONE = '";
        gql += (user.zone.empty() ? get_local_zone(conn) : user.zone);
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            return row[0];
        }

        return std::nullopt;
    }

    auto id(rxComm& conn, const group& group) -> std::optional<std::string>
    {
        std::string gql = "select USER_GROUP_ID where USER_TYPE = 'rodsgroup' and USER_GROUP_NAME = '";
        gql += group.name;
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            return row[0];
        }

        return std::nullopt;
    }

    auto type(rxComm& conn, const user& user) -> std::optional<user_type>
    {
        std::string gql = "select USER_TYPE where USER_TYPE != 'rodsgroup' and USER_NAME = '";
        gql += unique_name(conn, user);
        gql += "' and USER_ZONE = '";
        gql += (user.zone.empty() ? get_local_zone(conn) : user.zone);
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            return to_user_type(row[0]);
        }

        return std::nullopt;
    }

    auto auth_names(rxComm& conn, const user& user) -> std::vector<std::string>
    {
        std::vector<std::string> auth_names;

        std::string gql = "select USER_DN where USER_TYPE != 'rodsgroup' and USER_NAME = '";
        gql += unique_name(conn, user);
        gql += "' and USER_ZONE = '";
        gql += (user.zone.empty() ? get_local_zone(conn) : user.zone);
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            auth_names.push_back(row[0]);
        }

        return auth_names;
    }


    auto user_is_member_of_group(rxComm& conn, const group& group, const user& user) -> bool
    {
        std::string gql = "select USER_ID where USER_TYPE != 'rodsgroup' and USER_NAME = '";
        gql += unique_name(conn, user);
        gql += "' and USER_ZONE = '";
        gql += (user.zone.empty() ? get_local_zone(conn) : user.zone);
        gql += "' and USER_GROUP_NAME = '";
        gql += group.name;
        gql += "'";

        for (auto&& row : irods::query{&conn, gql}) {
            static_cast<void>(row);
            return true;
        }

        return false;
    }

    // Utility

    auto unique_name(rxComm& conn, const user& user) -> std::string
    {
        auto name = user.name;

        if (user.zone != get_local_zone(conn)) {
            name += '#';
            name += user.zone;
        }

        return name;
    }

    auto operator<<(std::ostream& out, const user& user) -> std::ostream&
    {
        return out << json{{"name", user.name}, {"zone", user.zone}}.dump();
    }

    auto operator<<(std::ostream& out, const group& group) -> std::ostream&
    {
        return out << json{{"name", group.name}}.dump();
    }
} // namespace irods::experimental::administration::NAMESPACE_IMPL

