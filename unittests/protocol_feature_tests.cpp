/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <Runtime/Runtime.h>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(protocol_feature_tests)

BOOST_AUTO_TEST_CASE( activate_preactivate_feature ) try {
   tester c( setup_policy::none );
   const auto& pfm = c.control->get_protocol_feature_manager();

   c.produce_block();

   // Cannot set latest bios contract since it requires intrinsics that have not yet been whitelisted.
   BOOST_CHECK_EXCEPTION( c.set_code( config::system_account_name, contracts::eosio_bios_wasm() ),
                          wasm_exception, fc_exception_message_is("env.is_feature_activated unresolveable")
   );

   // But the old bios contract can still be set.
   c.set_code( config::system_account_name, contracts::before_preactivate_eosio_bios_wasm() );
   c.set_abi( config::system_account_name, contracts::before_preactivate_eosio_bios_abi().data() );

   auto t = c.control->pending_block_time();
   c.control->abort_block();
   BOOST_REQUIRE_EXCEPTION( c.control->start_block( t, 0, {digest_type()} ), protocol_feature_exception,
                            fc_exception_message_is( "protocol feature with digest '0000000000000000000000000000000000000000000000000000000000000000' is unrecognized" )
   );

   auto d = pfm.get_builtin_digest( builtin_protocol_feature_t::preactivate_feature );

   BOOST_REQUIRE( d );

   // Activate PREACTIVATE_FEATURE.
   c.schedule_protocol_features_wo_preactivation({ *d });
   c.produce_block();

   // Now the latest bios contract can be set.
   c.set_code( config::system_account_name, contracts::eosio_bios_wasm() );
   c.set_abi( config::system_account_name, contracts::eosio_bios_abi().data() );

   c.produce_block();

   BOOST_CHECK_EXCEPTION( c.push_action( config::system_account_name, N(reqactivated), config::system_account_name,
                                          mutable_variant_object()("feature_digest",  digest_type()) ),
                           eosio_assert_message_exception,
                           eosio_assert_message_is( "protocol feature is not activated" )
   );

   c.push_action( config::system_account_name, N(reqactivated), config::system_account_name, mutable_variant_object()
      ("feature_digest",  *d )
   );

   c.produce_block();

   // Ensure validator node accepts the blockchain

   tester c2(setup_policy::none, db_read_mode::SPECULATIVE);
   push_blocks( c, c2 );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( activate_and_restart ) try {
   tester c( setup_policy::none );
   const auto& pfm = c.control->get_protocol_feature_manager();

   auto pfs = pfm.get_protocol_feature_set(); // make copy of protocol feature set

   auto d = pfm.get_builtin_digest( builtin_protocol_feature_t::preactivate_feature );
   BOOST_REQUIRE( d );

   BOOST_CHECK( !c.control->is_builtin_activated( builtin_protocol_feature_t::preactivate_feature ) );

   // Activate PREACTIVATE_FEATURE.
   c.schedule_protocol_features_wo_preactivation({ *d });
   c.produce_blocks(2);

   auto head_block_num = c.control->head_block_num();

   BOOST_CHECK( c.control->is_builtin_activated( builtin_protocol_feature_t::preactivate_feature ) );

   c.close();
   c.open( std::move( pfs ), nullptr );

   BOOST_CHECK_EQUAL( head_block_num, c.control->head_block_num() );

   BOOST_CHECK( c.control->is_builtin_activated( builtin_protocol_feature_t::preactivate_feature ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( double_preactivation ) try {
   tester c( setup_policy::preactivate_feature_and_new_bios );
   const auto& pfm = c.control->get_protocol_feature_manager();

   auto d = pfm.get_builtin_digest( builtin_protocol_feature_t::only_link_to_existing_permission );
   BOOST_REQUIRE( d );

   c.push_action( config::system_account_name, N(preactivate), config::system_account_name,
                  fc::mutable_variant_object()("feature_digest", *d), 10 );

   std::string expected_error_msg("protocol feature with digest '");
   {
      fc::variant v;
      to_variant( *d, v );
      expected_error_msg += v.get_string();
      expected_error_msg += "' is already pre-activated";
   }

   BOOST_CHECK_EXCEPTION(  c.push_action( config::system_account_name, N(preactivate), config::system_account_name,
                                          fc::mutable_variant_object()("feature_digest", *d), 20 ),
                           protocol_feature_exception,
                           fc_exception_message_is( expected_error_msg )
   );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( double_activation ) try {
   tester c( setup_policy::preactivate_feature_and_new_bios );
   const auto& pfm = c.control->get_protocol_feature_manager();

   auto d = pfm.get_builtin_digest( builtin_protocol_feature_t::only_link_to_existing_permission );
   BOOST_REQUIRE( d );

   BOOST_CHECK( !c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );

   c.preactivate_protocol_features( {*d} );

   BOOST_CHECK( !c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );

   c.schedule_protocol_features_wo_preactivation( {*d} );

   BOOST_CHECK_EXCEPTION(  c.produce_block();,
                           block_validate_exception,
                           fc_exception_message_starts_with( "attempted duplicate activation within a single block:" )
   );

   c.protocol_features_to_be_activated_wo_preactivation.clear();

   BOOST_CHECK( !c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );

   c.produce_block();

   BOOST_CHECK( c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );

   c.produce_block();

   BOOST_CHECK( c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( require_preactivation_test ) try {
   tester c( setup_policy::preactivate_feature_and_new_bios );
   const auto& pfm = c.control->get_protocol_feature_manager();

   auto d = pfm.get_builtin_digest( builtin_protocol_feature_t::only_link_to_existing_permission );
   BOOST_REQUIRE( d );

   BOOST_CHECK( !c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );

   c.schedule_protocol_features_wo_preactivation( {*d} );
   BOOST_CHECK_EXCEPTION( c.produce_block(),
                          protocol_feature_exception,
                          fc_exception_message_starts_with( "attempted to activate protocol feature without prior required preactivation:" )
   );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( only_link_to_existing_permission_test ) try {
   tester c( setup_policy::preactivate_feature_and_new_bios );
   const auto& pfm = c.control->get_protocol_feature_manager();

   auto d = pfm.get_builtin_digest( builtin_protocol_feature_t::only_link_to_existing_permission );
   BOOST_REQUIRE( d );

   c.create_accounts( {N(alice), N(bob), N(charlie)} );

   BOOST_CHECK_EXCEPTION(  c.push_action( config::system_account_name, N(linkauth), N(bob), fc::mutable_variant_object()
                              ("account", "bob")
                              ("code", name(config::system_account_name))
                              ("type", "")
                              ("requirement", "test" )
                           ), permission_query_exception,
                           fc_exception_message_is( "Failed to retrieve permission: test" )
   );

   BOOST_CHECK_EXCEPTION(  c.push_action( config::system_account_name, N(linkauth), N(charlie), fc::mutable_variant_object()
                              ("account", "charlie")
                              ("code", name(config::system_account_name))
                              ("type", "")
                              ("requirement", "test" )
                           ), permission_query_exception,
                           fc_exception_message_is( "Failed to retrieve permission: test" )
   );

   c.push_action( config::system_account_name, N(updateauth), N(alice), fc::mutable_variant_object()
      ("account", "alice")
      ("permission", "test")
      ("parent", "active")
      ("auth", authority(get_public_key("testapi", "test")))
   );

   c.produce_block();

   // Verify the incorrect behavior prior to ONLY_LINK_TO_EXISTING_PERMISSION activation.
   c.push_action( config::system_account_name, N(linkauth), N(bob), fc::mutable_variant_object()
      ("account", "bob")
      ("code", name(config::system_account_name))
      ("type", "")
      ("requirement", "test" )
   );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   // Verify the correct behavior after ONLY_LINK_TO_EXISTING_PERMISSION activation.
   BOOST_CHECK_EXCEPTION(  c.push_action( config::system_account_name, N(linkauth), N(charlie), fc::mutable_variant_object()
                              ("account", "charlie")
                              ("code", name(config::system_account_name))
                              ("type", "")
                              ("requirement", "test" )
                           ), permission_query_exception,
                           fc_exception_message_is( "Failed to retrieve permission: test" )
   );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( subjective_restrictions_test ) try {
   tester c( setup_policy::none );
   const auto& pfm = c.control->get_protocol_feature_manager();

   auto restart_with_new_pfs = [&c]( protocol_feature_set&& pfs ) {
      c.close();
      c.open(std::move(pfs), nullptr);
   };

   auto get_builtin_digest = [&pfm]( builtin_protocol_feature_t codename ) -> digest_type {
      auto res = pfm.get_builtin_digest( codename );
      BOOST_REQUIRE( res );
      return *res;
   };

   auto preactivate_feature_digest = get_builtin_digest( builtin_protocol_feature_t::preactivate_feature );
   auto only_link_to_existing_permission_digest = get_builtin_digest( builtin_protocol_feature_t::only_link_to_existing_permission );

   auto invalid_act_time = fc::time_point::from_iso_string( "2200-01-01T00:00:00" );
   auto valid_act_time = fc::time_point{};

   // First, test subjective_restrictions on feature that can be activated WITHOUT preactivation (PREACTIVATE_FEATURE)

   c.schedule_protocol_features_wo_preactivation({ preactivate_feature_digest });
   // schedule PREACTIVATE_FEATURE activation (persists until next successful start_block)

   subjective_restriction_map custom_subjective_restrictions = {
      { builtin_protocol_feature_t::preactivate_feature, {invalid_act_time, false, true} }
   };
   restart_with_new_pfs( make_protocol_feature_set(custom_subjective_restrictions) );
   // When a block is produced, the protocol feature activation should fail and throw an error
   BOOST_CHECK_EXCEPTION(  c.produce_block(),
                           protocol_feature_exception,
                           fc_exception_message_starts_with(
                              std::string(c.control->head_block_time()) +
                              " is too early for the earliest allowed activation time of the protocol feature"
                           )
   );
   BOOST_CHECK_EQUAL( c.protocol_features_to_be_activated_wo_preactivation.size(), 1 );

   // Revert to the valid earliest allowed activation time, however with enabled == false
   custom_subjective_restrictions = {
      { builtin_protocol_feature_t::preactivate_feature, {valid_act_time, false, false} }
   };
   restart_with_new_pfs( make_protocol_feature_set(custom_subjective_restrictions) );
   // This should also fail, but with different exception
   BOOST_CHECK_EXCEPTION(  c.produce_block(),
                           protocol_feature_exception,
                           fc_exception_message_is(
                              std::string("protocol feature with digest '") +
                              std::string(preactivate_feature_digest) +
                              "' is disabled"
                           )
   );
   BOOST_CHECK_EQUAL( c.protocol_features_to_be_activated_wo_preactivation.size(), 1 );

   // Revert to the valid earliest allowed activation time, however with subjective_restrictions enabled == true
   custom_subjective_restrictions = {
      { builtin_protocol_feature_t::preactivate_feature, {valid_act_time, false, true} }
   };
   restart_with_new_pfs( make_protocol_feature_set(custom_subjective_restrictions) );
   // Now it should be fine, the feature should be activated after the block is produced
   BOOST_CHECK_NO_THROW( c.produce_block() );
   BOOST_CHECK( c.control->is_builtin_activated( builtin_protocol_feature_t::preactivate_feature ) );
   BOOST_CHECK_EQUAL( c.protocol_features_to_be_activated_wo_preactivation.size(), 0 );

   // Second, test subjective_restrictions on feature that need to be activated WITH preactivation (ONLY_LINK_TO_EXISTING_PERMISSION)

   c.set_bios_contract();
   c.produce_block();

   custom_subjective_restrictions = {
      { builtin_protocol_feature_t::only_link_to_existing_permission, {invalid_act_time, true, true} }
   };
   restart_with_new_pfs( make_protocol_feature_set(custom_subjective_restrictions) );
   // It should fail
   BOOST_CHECK_EXCEPTION(  c.preactivate_protocol_features({only_link_to_existing_permission_digest}),
                           subjective_block_production_exception,
                           fc_exception_message_starts_with(
                              std::string(c.control->head_block_time() + fc::milliseconds(config::block_interval_ms)) +
                              " is too early for the earliest allowed activation time of the protocol feature"
                           )
   );

   // Revert with valid time and subjective_restrictions enabled == false
   custom_subjective_restrictions = {
      { builtin_protocol_feature_t::only_link_to_existing_permission, {valid_act_time, true, false} }
   };
   restart_with_new_pfs( make_protocol_feature_set(custom_subjective_restrictions) );
   // It should fail but with different exception
   BOOST_CHECK_EXCEPTION(  c.preactivate_protocol_features({only_link_to_existing_permission_digest}),
                           subjective_block_production_exception,
                           fc_exception_message_is(
                              std::string("protocol feature with digest '") +
                              std::string(only_link_to_existing_permission_digest)+
                              "' is disabled"
                           )
   );

   // Revert with valid time and subjective_restrictions enabled == true
   custom_subjective_restrictions = {
      { builtin_protocol_feature_t::only_link_to_existing_permission, {valid_act_time, true, true} }
   };
   restart_with_new_pfs( make_protocol_feature_set(custom_subjective_restrictions) );
   // Should be fine now, and activated in the next block
   BOOST_CHECK_NO_THROW( c.preactivate_protocol_features({only_link_to_existing_permission_digest}) );
   c.produce_block();
   BOOST_CHECK( c.control->is_builtin_activated( builtin_protocol_feature_t::only_link_to_existing_permission ) );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
