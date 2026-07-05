#include "test_framework.h"
#include "../../iridium_parser.h"

TEST(test_sbdix_success_parses_all_fields)
{
    iridium_sbd_session_t session = {0};
    ASSERT_OK(iridium_parser_sbd_session("+SBDIX: 0,12,1,8,50,0", &session));
    ASSERT_EQ(0, session.mo_status);
    ASSERT_EQ(12, session.momsn);
    ASSERT_EQ(1, session.mt_status);
    ASSERT_EQ(8, session.mtmsn);
    ASSERT_EQ(50, session.mt_length);
    ASSERT_EQ(0, session.mt_queued);
}

TEST(test_sbdsx_without_plus_prefix)
{
    iridium_sbd_session_t session = {0};
    ASSERT_OK(iridium_parser_sbd_session("SBDSX: 0,1,0,0,0,1", &session));
    ASSERT_EQ(1, session.mt_queued);
}

TEST(test_sbdix_no_network)
{
    iridium_sbd_session_t session = {0};
    ASSERT_OK(iridium_parser_sbd_session("+SBDIX: 32,0,0,0,0,0", &session));
    ASSERT_EQ(32, session.mo_status);
    ASSERT_FALSE(iridium_parser_mo_transfer_ok(session.mo_status));
}

TEST(test_sbdix_malformed_rejected)
{
    iridium_sbd_session_t session = {0};
    ASSERT_FALSE(iridium_parser_sbd_session("+SBDIX: 1,2,3", &session));
    ASSERT_FALSE(iridium_parser_sbd_session("", &session));
    ASSERT_FALSE(iridium_parser_sbd_session("+SBDIX: bad", &session));
}

TEST(test_mo_transfer_ok_codes)
{
    ASSERT_TRUE(iridium_parser_mo_transfer_ok(0));
    ASSERT_TRUE(iridium_parser_mo_transfer_ok(1));
    ASSERT_TRUE(iridium_parser_mo_transfer_ok(2));
    ASSERT_FALSE(iridium_parser_mo_transfer_ok(32));
    ASSERT_FALSE(iridium_parser_mo_transfer_ok(10));
}

TEST(test_csq_parsing)
{
    int csq = -1;
    ASSERT_OK(iridium_parser_csq("+CSQ: 4", &csq));
    ASSERT_EQ(4, csq);
}

TEST(test_copy_string_truncates_safely)
{
    char dest[8];
    ASSERT_FALSE(iridium_parser_copy_string(dest, sizeof(dest), "0123456789"));
    ASSERT_STR_EQ("0123456", dest);
}

TEST(test_fixture_mo_send_success)
{
    /* fixtures/mo_send_success.txt — recorded MO session success */
    iridium_sbd_session_t session = {0};
    ASSERT_OK(iridium_parser_sbd_session("+SBDIX: 0,42,1,17,25,0", &session));
    ASSERT_TRUE(iridium_parser_mo_transfer_ok(session.mo_status));
    ASSERT_EQ(25, session.mt_length);
}

TEST(test_fixture_mo_retry_no_network)
{
    /* fixtures/mo_retry_no_network.txt — first attempt, no service */
    iridium_sbd_session_t session = {0};
    ASSERT_OK(iridium_parser_sbd_session("+SBDIX: 32,0,0,0,0,0", &session));
    ASSERT_EQ(32, session.mo_status);
}

void run_sbd_parser_tests(void)
{
    printf("SBD parser tests\n");
    RUN_TEST(test_sbdix_success_parses_all_fields);
    RUN_TEST(test_sbdsx_without_plus_prefix);
    RUN_TEST(test_sbdix_no_network);
    RUN_TEST(test_sbdix_malformed_rejected);
    RUN_TEST(test_mo_transfer_ok_codes);
    RUN_TEST(test_csq_parsing);
    RUN_TEST(test_copy_string_truncates_safely);
    RUN_TEST(test_fixture_mo_send_success);
    RUN_TEST(test_fixture_mo_retry_no_network);
}
