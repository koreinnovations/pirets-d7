<?php

/**
 * @file
 * Contains code for making queries against the RETS server and parsing the
 * response.
 */

/**
 * Define the default User-Agent header
 */
define('PIRETS_DEFAULT_UA', 'PIRETS/3.2 ($Revision$; http://precisionintermedia.com/; garrett@precisionintermedia.com)');

/**
 * Make a login query and return the result.
 *
 * @param $credentials
 *   A string containing the username and password to use when connecting,
 *   separated with a colon: username:password. Will use the stored default
 *   credentials if NULL.
 * @return
 *   The result, as an object. See _pirets_query() documentation for more info.
 */
function pirets_query_login($credentials = NULL) {
  return _pirets_query('', 'login', $credentials);
}

/**
 * Make a METADATA-CLASS metadata query and return the result.
 *
 * @return
 *   The result, as an object. See _pirets_query() documentation for more info.
 */
function pirets_query_metadata_class() {
  $result = _pirets_query('?Type=METADATA-CLASS&ID=Property&Format=COMPACT', 'getmetadata');
  $result->data = _pirets_explode_compact($result->data_raw, 'METADATA-CLASS');
  return $result;
}

/**
 * Make a METADATA-TABLE metadata query and return the result.
 *
 * @param $class
 *   The property class to fetch metadata about.
 * @return
 *   The result, as an object. See _pirets_query() documentation for more info.
 */
function pirets_query_metadata_table($class) {
  $result = _pirets_query('?Type=METADATA-TABLE&Format=COMPACT&ID=Property:' . check_plain($class), 'getmetadata');
  $result->data = _pirets_explode_compact($result->data_raw, 'METADATA-TABLE');
  return $result;
}

/**
 * Make a METADATA-LOOKUP_TYPE metadata query and return the result.
 *
 * @param $lookup_id
 *   The name of the lookup to metadata about.
 * @return
 *   The result, as an object. See _pirets_query() documentation for more info.
 */
function pirets_query_metadata_lup_type($lookup_id) {
  $result = _pirets_query('?Type=METADATA-LOOKUP_TYPE&Format=COMPACT&ID=Property:' . check_plain($lookup_id), 'getmetadata');
  $result->data = _pirets_explode_compact($result->data_raw, 'METADATA-LOOKUP_TYPE');
  return $result;
}

/**
 * Return an image for a property listing.
 *
 * @param $mls_id
 *   The MLS ID number for the property in question.
 * @param $offset
 *   The offset of the photo to fetch.
 * @return
 *   The result, as an object. See _pirets_query() documentation for more info.
 */
function pirets_query_prop_img($mls_id, $offset) {
  return _pirets_query("?Type=Photo&Resource=Property&ID={$mls_id}:{$offset}&QueryType=DMQL2", 'getobject');
}

/**
 * Perform a search query and return the results.
 *
 * @param $class
 *   The property class to search in.
 * @param $query
 *   The terms to search by, as an array; each part of the array is a clause
 *   in a format like "FieldName=Value", like "L_Full_Baths>=2".
 * @param $fields
 *   The fields to request values for returned, as an array; one field per
 *   element.
 * @param $limit
 *   The maximum number of results to return.
 * @param $offset
 *   The search result offset to start on.
 * @return
 *   The result, as an object. See _pirets_query() documentation for more info.
 */
function pirets_query_search($class, $query, $fields, $limit, $offset) {
  $result = _pirets_query("?SearchType=Property&Format=COMPACT&QueryType=DMQL2&Limit={$limit}&Offset={$offset}&Class={$class}&Query=(" . implode('),(', $query) . ')&Select=' . implode(',', $fields), 'search');
  $result->data = _pirets_explode_compact($result->data_raw);
  return $result;
}

/**
 * Make a query against the server and return the result.
 *
 * We're not using drupal_http_request() here because it doesn't support digest
 * authentication, required by RETS. We're going to use PHP's cURL library
 * instead. (cURL also lets us easily request and decode compressed responses,
 * though few RETS servers seem to support this.)
 *
 * @param $query
 *   The basic query string.
 * @param $type
 *   Either one of 'login', 'search', 'getobject' or 'getmetadata', in which
 *   case the function will attempt to connect to the address in the
 *   "pirets_server_{$type}_uri" variable.
 * @param $credentials
 *   A string containing the username and password to use when connecting,
 *   separated with a colon: username:password. Will use the stored default
 *   credentials if NULL.
 * @return
 *   The result, as an object. Relevant keys here include:
 *   'data_raw': The unprocessed data returned from the query, without headers.
 *   'headers': An array of headers.
 *   'response_code': The HTTP response code.
 */
function _pirets_query($query, $type, $credentials = NULL) {
  // This function calls itself recursively to log in after a query seems to
  // fail due to lack of authentication. To avoid an infinite loop…
  static $is_retry = FALSE;
  // Build the URI to connect to.
  $uri = variable_get("pirets_server_{$type}_uri", '');

  // It seems that + characters are the only things that need to be encoded.
  $full_query = $uri . str_replace(array('+'), array('%2B'), $query);

  if ($type === 'search') {
    $full_query .= variable_get('pirets_limit_suffix', '');
  }

  $cxn = curl_init($full_query);
  // Set cURL options and headers. Note that we're not doing
  // curl_setopt($cxn, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
  // …because RETS wants a specially-calculated client nonce (cnonce) value,
  // and who knows how cURL is creating its own. So we're going to handle all
  // the digest authentication stuff ourselves. A pain, but it works.
  curl_setopt($cxn, CURLOPT_RETURNTRANSFER, TRUE);
  $custom_ua = variable_get('pirets_server_ua', '');
  $ua = $custom_ua === '' ? PIRETS_DEFAULT_UA : $custom_ua;
  curl_setopt($cxn, CURLOPT_USERAGENT, $ua);

  if (variable_get('pirets_server_gzip', TRUE)) {
    curl_setopt($cxn, CURLOPT_ENCODING, '');
  }

  $headers = array('RETS-Version: RETS/1.5');

  if ($type === 'getobject') {
    $headers[] = 'Accept: image/jpeg';
  }
  else {
    $headers[] = 'Accept: */*';
    $headers[] = 'Accept-Charset: utf-8,ISO-8859-1,Windows-1252,us-ascii';
  }
  if ($type !== 'login') {
    $cookie = variable_get('pirets_cookie', FALSE);
    if ($cookie) {
      $headers[] = 'Cookie: ' . $cookie;
    }
  }

  // Build the Authorization header
  $d_parts = variable_get('pirets_digest_parts', FALSE);
  if ($d_parts !== FALSE) {
    if ($credentials === NULL) {
      $d_user = variable_get('pirets_server_user', '');
      $d_pass = variable_get('pirets_server_pass', '');
    }
    else {
      list($d_user, $d_pass) = explode(':', $credentials);
    }

    // Create a request ID. I don't think this needs to be anything in
    // particular…? RETS 1.5, 3.5
    $req_id = crc32($full_query);
    $headers[] = 'RETS-Request-ID: ' . $req_id;

    // Calculate the cnonce according to the RETS 1.5, 4.1.2
    $cnonce = md5("{$ua}:{$d_pass}:{$req_id}:{$d_parts['nonce']}");

    $q_parts = parse_url($full_query);

    $nc = variable_get('pirets_nc', 0);
    // The magic number below is the maximum value dechex() can convert:
    // http://php.net/dechex
    variable_set('pirets_nc', $nc === 4294967295 ? 0 : ++$nc);
    $nc = str_pad(dechex($nc), 8, '0', STR_PAD_LEFT);

    $response = md5(md5("{$d_user}:{$d_parts['realm']}:{$d_pass}") . ":{$d_parts['nonce']}:{$nc}:{$cnonce}:auth:" . md5('GET:' . $q_parts['path']));

    $headers[] = "Authorization: Digest username=\"{$d_user}\", realm=\"{$d_parts['realm']}\", uri=\"{$q_parts['path']}\", nonce=\"{$d_parts['nonce']}\", opaque=\"{$d_parts['opaque']}\", nc={$nc}, qop=auth, cnonce=\"{$cnonce}\", response=\"{$response}\"";
  }

  curl_setopt($cxn, CURLOPT_HTTPHEADER, $headers);

  // We want the response to contain the HTTP headers.
  curl_setopt($cxn, CURLOPT_HEADER, TRUE);

  $response = curl_exec($cxn);
  curl_close($cxn);
  $result = new stdClass();
  $result->uri = $full_query;
  $result->headers_out = $headers;
  // Parse response.
  // The cURL library returns several sets of headers with digest auth. We'll
  // find the last one which starts with "HTTP" and assume that that one is the
  // proper headers. Those before will be tossed; those after will be combined
  // and assumed to be the body text.
  $chunks = preg_split('/\r\n\r\n|\n\n/', $response, -1, PREG_SPLIT_OFFSET_CAPTURE);
  $count = count($chunks);
  $possible_header = $count - 2;
  while ($possible_header > -1 && strpos($chunks[$possible_header][0], 'HTTP') !== 0) {
    $possible_header--;
  }
  // Slice out the non-header from the original response starting at the
  // position of the first chunk which wasn't a header.
  $result->data_raw = substr($response, $chunks[$possible_header + 1][1]);
  $result->headers_raw = $chunks[$possible_header][0];
  $headers_split = preg_split('/\r\n|\n/', $result->headers_raw);
  list($result->protocol, $result->response_code, $result->error) = explode(' ', trim(array_shift($headers_split)), 3);
  $result->response_code = intval($result->response_code);
  $result->headers = array();
  while ($line = trim(array_shift($headers_split))) {
    list($header, $value) = explode(':', $line, 2);
    if (isset($result->headers[$header]) && $header === 'Set-Cookie') {
      // RFC 2109: the Set-Cookie response header comprises the token Set-
      // Cookie:, followed by a comma-separated list of one or more cookies.
      $result->headers['Set-Cookie'] .= ',' . trim($value);
    }
    else {
      $result->headers[$header] = trim($value);
    }
  }

  // $result is done at this point.

  if (isset($result->headers['Set-Cookie'])) {
    // Save the cookie for later
    variable_set('pirets_cookie', $result->headers['Set-Cookie']);
  }

  // Save digest auth stuff for later, if it was provided
  if (isset($result->headers['WWW-Authenticate'])) {
    $d_parts = array();
    preg_match_all('/([^ =,]*)="([^"]*)"/', $result->headers['WWW-Authenticate'], $matches, PREG_SET_ORDER);
    foreach ($matches as $match) {
      $d_parts[$match[1]] = $match[2];
    }
    variable_set('pirets_digest_parts', $d_parts);
  }

  // If we get a 401 or 412, try logging in again and then trying the query
  // again.

  if (($result->response_code === 401 || $result->response_code === 412)) {
    if ($is_retry) {
      watchdog('pirets', 'The RETS server says we\'re unauthorized, even after attempting to log in again. Possible causes: Credentials (username and password) are incorrect; server doesn\'t like the User-Agent header; Garrett\'s digest authentication response creation code is wrong. Query: @query', array('@query' => $full_query), WATCHDOG_ERROR);
    }
    else {
      if ($type !== 'login') {
        pirets_query_login($credentials);
      }
      $is_retry = TRUE;
      return _pirets_query($query, $type, $credentials);
    }
  }
  elseif ($result->response_code !== 200) {
    watchdog('pirets', 'RETS query appears to have failed. Query: @query - Error: @respcode (@error)', array('@query' => $full_query, '@respcode' => $result->response_code, '@error' => $result->error), WATCHDOG_ERROR);
  }

  // Do we need to convert the response text encoding?
  $assenc = variable_get('pirets_server_assenc', 'none');

  if ($type !== 'getobject') {
    // Don't iconv images!
    if ($assenc !== 'none') {
      $result->data_raw = iconv($assenc, 'UTF-8//TRANSLIT', $result->data_raw);
    }
    elseif (isset($result->headers['Content-Type']) && preg_match('/charset=([\w-]+)/', $result->headers['Content-Type'], $enc_matches) && $enc_matches[1] !== 'utf-8' && $enc_matches[1] !== 'us-ascii') {
      $result->data_raw = iconv($enc_matches[1], 'UTF-8//TRANSLIT', $result->data_raw);
    }
  }

  return $result;
}

/**
 * Parse a COMPACT-ENCODED response.
 *
 * @param $xml
 *   The XML to parse.
 * @param $child
 *   If not null, the XML child to parse. Data outside the child XML node will
 *   be ignored.
 * @return
 *   An array of associative arrays containing the values.
 */
function _pirets_explode_compact($data, $child = NULL) {
  $xml = new SimpleXMLElement($data);

  $return = array();
  $headers = array();
  if ($child !== NULL) {
    $xml = $xml->{$child};
  }
  if (isset($child->DELIMETER[0])) {
    $delim = chr(hexdec(strval($xml->DELIMITER[0]['value'])));
  }
  else {
    $delim = "\t";
  }
  // These substr() calls are trimming off the tabs at the beginning and end
  // of the rows without hacking off tabs representing blank columns as trim()
  // would.
  $headers = explode($delim, substr(strval($xml->COLUMNS[0]), 1, -1));
  foreach ($xml->DATA as $row) {
    $vals = explode($delim, substr($row, 1, -1));
    // Note that some idiot RETS providers may be serving fields with names
    // which are entirely numeric; for example, the list price will be in field
    // "173." Care must be taken that we don't prompt PHP to reindex arrays with
    // field data, because the indexes will change on fields with these numeric
    // field names, causing trouble down the line. For example, the line below
    // used to use array_merge() to combine the array with the crc32 value, but
    // this was prompting reindexing. Now we're using the + operator, which
    // achieves the desired result without reindexing.
    $return[] = array('crc32' => crc32($row)) + array_combine($headers, $vals);
  }
  return $return;
}

/**
 * Returns all property classes.
 *
 * @param $update
 *   If TRUE, updated class data will be fetch from the RETS server and merged
 *   with the current class data (if any).
 * @return
 *   The class data as an associative array.
 */
function pirets_classes_fetch($update = FALSE) {
  $classes = variable_get('pirets_classes', array());
  if ((!count($classes) || $update) && variable_get('pirets_server_getmetadata_uri', FALSE)) {
    drupal_set_message(t('Fetching updated class data from the RETS server.'));
    $result = pirets_query_metadata_class();
    $types = array();
    foreach ($result->data as $proptype) {
      // If this class type already exists in $classes, copy over the data from
      // $classes. However, we don't want to keep *all* data in $classes, as it
      // could (theoretically) contain classes which don't exist anymore.
      if (isset($classes[$proptype['ClassName']])) {
        $types[$proptype['ClassName']] = $classes[$proptype['ClassName']];
      }
      else {
        $types[$proptype['ClassName']] = array(
          'visible' => $proptype['VisibleName'],
          'enabled' => TRUE,
          'lifetime' => 28800,
          'last_update' => 0,
        );
      }
    }
    $classes = $types;
    variable_set('pirets_classes', $classes);
  }
  return $classes;
}

/**
 * Returns all RETS fields.
 *
 * @param $update
 *   If TRUE, updated field data will be fetch from the RETS server and merged
 *   with the current field data (if any).
 * @return
 *   The field data as an associative array.
 */
function pirets_fields_fetch($update = FALSE) {
  $cached = cache_get('pirets_fields');
  if ($cached === 0) {
    $rez = db_query('SELECT * FROM {pirets_fields}');
    $fields = array();
    while ($field = db_fetch_array($rez)) {
      $fields[$field['system_name']] = $field;
    }
    if (count($fields) && !$update) {
      cache_set('pirets_fields', $fields);
    }
  }
  else {
    $fields = $cached->data;
  }
  if ((!count($fields) || $update) && variable_get('pirets_server_getmetadata_uri', FALSE)) {
    drupal_set_message(t('Fetching updated field data from the RETS server.'));
    $rets_fields = array();
    // First, get all fields for all classes (phew!).
    foreach (array_keys(pirets_classes_fetch()) as $class) {
      $result = pirets_query_metadata_table($class);
      foreach ($result->data as $field) {
        if (!isset($rets_fields[$field['SystemName']])) {
          $rets_fields[$field['SystemName']] = array(
            'system_name' => $field['SystemName'],
            'cck' => isset($fields[$field['SystemName']]['cck']) ? $fields[$field['SystemName']]['cck'] : 0,
            'data_type' => $field['DataType'],
            'max_len' => $field['MaximumLength'],
            'prec' => $field['Precision'] === '' ? 0 : $field['Precision'],
            'long_name' => $field['LongName'],
            'classes' => array($class),
            'lookup' => $field['LookupName'],
            'correlation' => isset($fields[$field['SystemName']]['correlation']) ? $fields[$field['SystemName']]['correlation'] : '',
            'lup_multi' => intval($field['MaxSelect']) > 0 ? 1 : 0,
          );
        }
        else {
          $rets_fields[$field['SystemName']]['classes'][] = $class;
        }
      }
    }
    // Now update everything in the database. This is kind of a brute force
    // way of doing this, but it works.
    // TODO Please review the conversion of this statement to the D7 database API syntax.
    /* db_query('DELETE FROM {pirets_fields}') */
    db_delete('pirets_fields')
  ->execute();
    foreach ($rets_fields as &$field) {
      $field['classes'] = implode(',', $field['classes']);
      drupal_write_record('pirets_fields', $field);
    }
    $fields = $rets_fields;
    cache_set('pirets_fields', $fields);
    cache_clear_all('pirets_fields_active', 'cache');
  }
  return $fields;
}

/**
 * Returns active RETS fields.
 *
 * @return
 *   The active field data, keyed by system name.
 */
function pirets_fields_active_fetch() {
  $cached = cache_get('pirets_fields_active');
  if ($cached === 0) {
    $rez = db_query('SELECT * FROM {pirets_fields} WHERE cck = :cck', array(':cck' => 1));
    $fields = array();
    while ($field = db_fetch_array($rez)) {
      $fields[$field['system_name']] = $field;
    }
    cache_set('pirets_fields_active', $fields);
    return $fields;
  }
  else {
    return $cached->data;
  }
}

/**
 * Returns lookups for a lookup field.
 *
 * @param $lid
 *   The lookup ID.
 * @param $update
 *   If true, the lookup values will be force-updated.
 * @return
 *   The lookup values for the $lid passed.
 */
function pirets_lookups_fetch($lid, $update = FALSE) {
  if (!$update) {
    $cached = cache_get('pirets_lups_' . $lid);
    if ($cached !== 0) {
      return $cached->data;
    }
    else {
      $rez = db_query('SELECT * FROM {pirets_lookups} WHERE lid = :lid', array(':lid' => $lid));
      $lups = array();
      while ($lup = db_fetch_array($rez)) {
        $lups[$lup['value']] = $lup;
      }
    }
  }
  else {
    $result = pirets_query_metadata_lup_type($lid);
    $lups = array();
    // TODO Please review the conversion of this statement to the D7 database API syntax.
    /* db_query('DELETE FROM {pirets_lookups} WHERE lid = "%s"', array($lid)) */
    db_delete('pirets_lookups')
  ->condition('lid', array($lid))
  ->execute();
    foreach ($result->data as $lup) {
      $record = array(
        'lid' => $lid,
        'long_value' => $lup['LongValue'],
        'value' => $lup['Value'],
      );
      drupal_write_record('pirets_lookups', $record);
      $lups[$record['value']] = $record;
    }
  }
  cache_set('pirets_lups_' . $lid, $lups);
  return $lups;
}
