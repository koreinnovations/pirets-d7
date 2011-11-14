<?php

/**
 * @file
 * Callbacks to perform administrative tasks for the PIRETS module.
 */

/**
 * Display basic status info about the module.
 */
function pirets_settings_main() {
  // Rather than include install.inc to get these status constants, we'll just
  // dupe them here.
  define('REQUIREMENT_INFO', -1);
  define('REQUIREMENT_OK', 0);
  define('REQUIREMENT_WARNING', 1);
  define('REQUIREMENT_ERROR', 2);

  $configged = array(
    'severity' => REQUIREMENT_OK,
    'value' => t('Configured'),
  );
  $not_configged = array(
    'severity' => REQUIREMENT_ERROR,
    'value' => t('Not configured'),
  );
  $not_config_warn = array(
    'severity' => REQUIREMENT_WARNING,
    'value' => t('Not configured'),
  );

  module_load_include('inc', 'pirets', 'pirets.connect');
  $items = array(
    'server' => array(
      'title' => l(t('Server credentials'), 'admin/config/pirets/server'),
      'description' => t('Enter server credentials and test server.'),
    ),
    'classes' => array(
      'title' => l(t('Property classes'), 'admin/config/pirets/classes'),
      'description' => t('Fetch property classes.'),
    ),
    'fields' => array(
      'title' => l(t('Useful field selection'), 'admin/config/pirets/fields/all'),
      'description' => t('Fetch RETS fields and select useful fields.'),
    ),
    'corr' => array(
      'title' => l(t('Field correlation'), 'admin/config/pirets/fields/useful'),
      'description' => t('Correlate <em>MLS ID number</em> and <em>Sale status</em> fields.'),
    ),
    'status' => array(
      'title' => l(t('Sale statuses'), 'admin/config/pirets/fields/limits'),
      'description' => t('Select useful sale status(es).'),
    ),
    'pix_modules' => array(
      'title' => l(t('Photo support - modules'), 'admin/modules'),
      'description' => t('For optional listing photo support, install the <a href="http://drupal.org/project/imagefield">ImageField</a> and <a href="http://drupal.org/project/imagecache">ImageCache</a> modules and their dependencies.'),
    ),
    'pix_corr' => array(
      'title' => l(t('Photo support - field correlation'), 'admin/config/pirets/fields/useful'),
      'description' => t('Correlate a <em>Picture count</em> field.'),
    ),
    'pix_imagecache' => array(
      'title' => l(t('Photo support - ImageCache preset'), 'admin/structure/imagecache'),
      'description' => t('Create at least one ImageCache preset which has <em>PIRETS fetch</em> as its first action and use it to display images in the <em>Property images</em> CCK field.'),
    ),
  );
  // Check that server settings are set
  if (!variable_get('pirets_server_login_uri', FALSE) || !variable_get('pirets_server_search_uri', FALSE) || !variable_get('pirets_server_getobject_uri', FALSE) || !variable_get('pirets_server_getmetadata_uri', FALSE) || !variable_get('pirets_server_user', FALSE) || !variable_get('pirets_server_pass', FALSE)) {
    $items['server'] += $not_configged;
  }
  else {
    $items['server'] += $configged;
  }

  // Check that we have some property classes
  $items['classes'] += count(pirets_classes_fetch()) ? $configged : $not_configged;

  // Check that we have some fields
  $active = pirets_fields_active_fetch();
  $items['fields'] += count($active) ? $configged : $not_configged;

  // Check that we have necessary correlations
  $mls = FALSE;
  $status = FALSE;
  $photos = FALSE;
  foreach ($active as $field) {
    if ($field['correlation'] === 'mls_id') {
      $mls = TRUE;
    }
    elseif ($field['correlation'] === 'sale_status') {
      $status = TRUE;
    }
    elseif ($field['correlation'] === 'pic_count') {
      $photos = TRUE;
    }
  }
  $items['corr'] += ($mls && $status) ? $configged : $not_configged;

  // Check that some useful sale status(es) have been selected
  $items['status'] += variable_get('pirets_limit_sstatuses', FALSE) ? $configged : $not_configged;

  // Are required modules installed for image support?
  $items['pix_modules'] += (module_exists('imagecache') && module_exists('imagefield')) ? $configged : $not_config_warn;

  // Is the picture count field correlated?
  $items['pix_corr'] += $photos ? $configged : $not_config_warn;

  // Check for an ImageCache preset which is fetching images
  $ic_ready = FALSE;
  if (module_exists('imagecache')) {
    $presets = imagecache_presets();
    foreach ($presets as $preset) {
      if ($preset['actions'][0]['action'] === 'pirets_fetch') {
        $ic_ready = TRUE;
        continue;
      }
    }
  }
  $items['pix_imagecache'] += $ic_ready ? $configged : $not_config_warn;

  return '<h3>' . t('Configuration checklist') . '</h3>' . theme('status_report', $items);
}

/**
 * Server connection settings form.
 */
function pirets_sets_server_form($form, &$form_state) {
  $weight = 0;
  $iconv_good = function_exists('iconv');
  $form = array(
    'rets_server_login_uri' => array(
      '#type' => 'textfield',
      '#title' => t('RETS server login URI'),
      '#default_value' => variable_get('pirets_server_login_uri', ''),
      '#required' => TRUE,
      '#weight' => $weight += 10,
    ),
    'rets_server_user' => array(
      '#type' => 'textfield',
      '#title' => t('RETS server username'),
      '#default_value' => variable_get('pirets_server_user', ''),
      '#required' => TRUE,
      '#weight' => $weight += 10,
    ),
    'rets_server_pass' => array(
      '#type' => 'textfield',
      '#title' => t('RETS server password'),
      '#default_value' => variable_get('pirets_server_pass', ''),
      '#required' => TRUE,
      '#weight' => $weight += 10,
    ),
    'adv_fset' => array(
      '#type' => 'fieldset',
      '#title' => t('Advanced options'),
      '#weight' => $weight += 10,
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
      'rets_server_assenc' => array(
        '#type' => 'select',
        '#title' => t('Assume server response encoding'),
        '#options' => array(
          'none' => t('Do not assume encoding (trust server)'),
          'Windows-1252' => t('Windows-1252'),
          'ISO-8859-1' => t('ISO-8859-1'),
        ),
        '#description' => $iconv_good ? t('PIRETS may automatically re-encode server responses based on the encoding the server says its response is in. However, some servers may misrepresent this. If you see strange &ldquo;garbage&rdquo; characters in listings or see &ldquo;String could not be parsed as XML&rdquo; errors, you may need to change this value.') : t('PIRETS may automatically re-encode server responses based on the encoding the server says its response is in. However, the server may occasionally misrepresent its response. If you see strange &ldquo;garbage&rdquo; characters in listings or see &ldquo;String could not be parsed as XML&rdquo; errors, you may need to change this value. <strong>This feature is disabled because this PHP installation does not support iconv.</strong>'),
        '#default_value' => $iconv_good ? variable_get('pirets_server_assenc', 'none') : 'none',
        '#disabled' => !$iconv_good,
        '#weight' => $weight += 10,
      ),
      'rets_server_gzip' => array(
        '#type' => 'checkbox',
        '#title' => t('Request compressed responses from server'),
        '#description' => t('If the RETS server supports compressed responses, this can greatly reduce the amount of time spent downloading responses from the server. Disabling this feature is strongly discouraged unless problems are encountered otherwise.'),
        '#default_value' => variable_get('pirets_server_gzip', TRUE),
        '#weight' => $weight += 10,
      ),
      'rets_server_ua' => array(
        '#type' => 'textfield',
        '#title' => t('User-Agent string'),
        '#description' => t('If left blank, PIRETS will represent itself as such to servers using its own User-Agent string. However, if it is necessary to &ldquo;spoof&rdquo; as another client, enter another User-Agent string here. This is useful when connecting to servers that will only respond to particular clients.'),
        '#default_value' => variable_get('pirets_server_ua', ''),
        '#weight' => $weight += 10,
      ),
    ),
    'submit' => array(
      '#type' => 'submit',
      '#value' => t('Submit and test server'),
      '#weight' => $weight += 10,
    ),
  );
  if ($server_info = variable_get('pirets_login_headers', FALSE)) {
    $subweight = 0;
    $form['server_info'] = array(
      '#type' => 'fieldset',
      '#title' => t('Server information'),
      '#weight' => $weight += 10,
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
      'search_uri' => array(
        '#type' => 'item',
        '#title' => t('Search URI'),
        '#value' => filter_xss_admin(variable_get('pirets_server_search_uri', '')),
        '#weight' => $subweight += 10,
      ),
      'getobj_uri' => array(
        '#type' => 'item',
        '#title' => t('GetObject URI'),
        '#value' => filter_xss_admin(variable_get('pirets_server_getobject_uri', '')),
        '#weight' => $subweight += 10,
      ),
      'getmeta_uri' => array(
        '#type' => 'item',
        '#title' => t('GetMetadata URI'),
        '#value' => filter_xss_admin(variable_get('pirets_server_getmetadata_uri', '')),
        '#weight' => $subweight += 10,
      ),
      'headers' => array(
        '#type' => 'item',
        '#title' => t('Server response headers'),
        '#value' => '<pre>' . filter_xss_admin(variable_get('pirets_login_headers', '')) . '</pre>',
      ),
    );
  }
  return $form;
}

/**
 * Validation of the server form.
 */
function pirets_sets_server_form_validate($form, &$form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  // Test connection to RETS server
  // First, temporarily set config variables…
  global $conf;
  $conftemp = $conf;
  foreach (array('user', 'pass', 'assenc', 'gzip', 'ua') as $opt) {
    $conf['pirets_server_' . $opt] = $form_state['values']['rets_server_' . $opt];
  }
  $conf['pirets_server_login_uri'] = $form_state['values']['rets_server_login_uri'];
  $response = pirets_query_login();
  $conf = $conftemp;
  if (!isset($response->response_code) || !$response->response_code) {
    form_set_error('submit', t('A valid response code could not be found in the response. This most likely means an error occurred before we could even check if the server was accessible. Please double-check the form values, but this is probably not your fault.'));
  }
  elseif ($response->response_code === 401) {
    form_set_error('rets_server_user', t('The server responded with a 401 error (unauthorized). The likely cause for this is that the username and/or password are incorrect.'));
  }
  elseif ($response->response_code === 404) {
    form_set_error('rets_server_login_uri', t('The server responded with a 404 error (file not found). The likely cause for this is that the server login URI is incorrect.'));
  }
  elseif ($response->response_code !== 200) {
    form_set_error('submit', t('The server was not successfully accessed; it responded with a @code error code (@errtext). Please check the settings and try again.', array('@code' => $response->response_code, '@errtext' => $response->error)));
  }
  elseif (!strpos($response->data_raw, 'Search') || !strpos($response->data_raw, 'GetObject')) {
    form_set_error('submit', t('The server was successfully accessed; however, the response given from the &ldquo;Login&rdquo; command was unacceptable (Search and/or GetMetadata paths were not found in the response). You may need to contact your RETS provider about this problem.'));
  }
  else {
    // Let's avoid re-fetching the response in the submit function.
    $form_state['values']['_response'] = $response;
  }
}

/**
 * Submission of the settings form.
 */
function pirets_sets_server_form_submit($form, &$form_state) {
  variable_set('pirets_server_login_uri', $form_state['values']['rets_server_login_uri']);
  preg_match('/^Search\s*=\s*(\S+)/m', $form_state['values']['_response']->data_raw, $search_matches);
  preg_match('/^GetObject\s*=\s*(\S+)/m', $form_state['values']['_response']->data_raw, $obj_matches);
  preg_match('/^GetMetadata\s*=\s*(\S+)/m', $form_state['values']['_response']->data_raw, $meta_matches);
  if ($search_matches[1]{0} === '/') {
    // These are relative paths. Need to prepend the server's base path.
    $url = parse_url($form_state['values']['rets_server_login_uri']);
    $base = "{$url['scheme']}://{$url['host']}";
    if (isset($url['port'])) {
      $base .= ":{$url['port']}";
    }
  }
  else {
    $base = '';
  }

  variable_set('pirets_server_search_uri', $base . $search_matches[1]);
  variable_set('pirets_server_getobject_uri', $base . $obj_matches[1]);
  variable_set('pirets_server_getmetadata_uri', $base . $meta_matches[1]);

  foreach (array('user', 'pass', 'assenc', 'gzip', 'ua') as $opt) {
    variable_set('pirets_server_' . $opt, $form_state['values']['rets_server_' . $opt]);
  }

  variable_set('pirets_login_headers', $form_state['values']['_response']->headers_raw);

  drupal_set_message(t('The configuration options have been saved.'));
  cache_clear_all();
}

/**
 * Class renaming and activation form.
 */
function pirets_sets_classes_form($form, &$form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  // The user must configure the server settings first.
  if (variable_get('pirets_server_getmetadata_uri', NULL) === NULL) {
    drupal_set_message(t('You must configure the RETS server settings first.'), 'error');
    drupal_goto('admin/config/pirets/server');
  }

  $classes = pirets_classes_fetch();

  $form = array(
    'classes' => array(
      '#theme' => 'pirets_classes_form',
      'class_names' => array(
        '#type' => 'value',
        '#value' => array(),
      ),
      '#weight' => 10,
    ),
    'submit' => array(
      '#type' => 'submit',
      '#value' => t('Save configuration'),
      '#weight' => 20,
    ),
  );

  $life_opts = array(0 => t('Every time')) + drupal_map_assoc(array(
    900, // 15 mins
    1800, // 30 mins
    3600, // 1 hour
    7200, // 2 hours
    10800, // 3 hours
    21600, // 6 hours
    28800, // 8 hours
    43200, // 12 hours
    86400, // 1 day
    172800, // 2 days
  ), 'format_interval');

  foreach ($classes as $sys => $class) {
    $form['classes']['class_names']['#value'][] = $sys;
    $form['classes'][$sys] = array(
      "{$sys}_class" => array(
        '#type' => 'item',
        '#value' => $sys,
      ),
      "{$sys}_visible" => array(
        '#type' => 'textfield',
        '#title' => t('Visible name'),
        '#default_value' => isset($class['visible']) ? check_plain($class['visible']) : '',
      ),
      "{$sys}_enabled" => array(
        '#type' => 'checkbox',
        '#title' => t('Enabled'),
        '#default_value' => isset($class['enabled']) ? $class['enabled'] : TRUE,
      ),
      "{$sys}_lifetime" => array(
        '#type' => 'select',
        '#options' => $life_opts,
        '#default_value' => isset($class['lifetime']) ? $class['lifetime'] : 28800,
      ),
    );
  }
  return $form;
}

/**
 * Save field settings.
 */
function pirets_sets_classes_form_submit($form, $form_state) {
  $classes = array();
  $select_opts = array();
  foreach ($form_state['values']['class_names'] as $class_name) {
    $classes[$class_name] = array(
      'visible' => $form_state['values'][$class_name . '_visible'],
      'enabled' => $form_state['values'][$class_name . '_enabled'] === 1,
      'lifetime' => intval($form_state['values'][$class_name . '_lifetime']),
    );
    $select_opts[] = "{$class_name}|{$form_state['values'][$class_name . '_visible']}";
  }
  variable_set('pirets_classes', $classes);
  // Add the classes as options for the class field.
  module_load_include('inc', 'content', 'includes/content.crud');
  $class_field = content_field_instance_read(array('type_name' => 'pirets_prop', 'field_name' => 'pirets_class'), TRUE);
  $class_field[0]['allowed_values'] = implode("\n", $select_opts);
  content_field_instance_update($class_field[0]);
  drupal_set_message(t('The configuration options have been saved.'));
  cache_clear_all();
}

/**
 * Useful field selection form.
 */
function pirets_sets_fields_form($form, &$form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  // The classes must be configured first.
  if (variable_get('pirets_classes', NULL) === NULL) {
    drupal_set_message(t('You must configure the RETS classes first.'), 'error');
    drupal_goto('admin/config/pirets/classes');
  }

  // Load all fields
  $fields = pirets_fields_fetch();
  $weight = 0;
  $form = array(
    'submit' => array(
      '#type' => 'submit',
      '#value' => t('Save configuration'),
    ),
    'fields' => array(
      '#weight' => 10,
    ),
  );
  foreach ($fields as $field) {
    $form['fields'][$field['system_name']] = array(
      '#type' => 'checkbox',
      '#title' => t('@sysname (@longname)', array('@sysname' => $field['system_name'], '@longname' => $field['long_name'])),
      '#default_value' => $field['cck'],
      '#weight' => $weight += 10,
    );
  }
  $form['submit']['#weight'] = $weight += 10;
  return $form;
}

/**
 * Create and remove CCK fields for the checked RETS fields.
 */
function pirets_sets_fields_form_submit($form, $form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  module_load_include('inc', 'content', 'includes/content.crud');
  $fields = pirets_fields_fetch();
  $weight = 20;
  foreach ($fields as $id => $field) {
    // If the field is checked on the form, but we're not currently using it…
    if ($form_state['values'][$id] && !$field['cck']) {
      $cck_field = array(
        'field_name' => 'pirets_' . $id,
        'type_name' => 'pirets_prop',
        'label' => $field['long_name'],
        'weight' => $weight += 10,
        'group' => 'group_pirets',
      );
      // What type of field are we creating?
      if ($field['lookup'] !== '') {
        // Lookup fields should always be text fields.
        $lookups = pirets_lookups_fetch($field['lookup'], TRUE);
        $values = array();
        $sort_by = array();
        foreach ($lookups as $lup) {
          $plain = check_plain($lup['long_value']);
          $sort_by[] = $plain;
          $values[] = "{$lup['value']}|{$lup['long_value']}";
        }
        array_multisort($sort_by, $values);
        $cck_field['type'] = 'text';
        $cck_field['max_length'] = $field['max_len'];
        $cck_field['widget_type'] = 'optionwidgets_select';
        $cck_field['allowed_values'] = implode("\n", $values);
        $cck_field['multiple'] = $field['lup_multi'];
      }
      else {
        if (in_array($field['data_type'], array('Boolean', 'Tiny', 'Small', 'Int', 'Long')) || ($field['data_type'] === 'Decimal' && $field['prec'] == 0)) {
          $cck_field['type'] = 'number_integer';
          $cck_field['widget_type'] = 'number';
        }
        elseif ($field['data_type'] === 'Decimal') {
          $cck_field['type'] = 'number_decimal';
          $cck_field['widget_type'] = 'number';
          // Note that RETS and this CCK field are using the term "precision"
          // differently
          $cck_field['precision'] = $field['max_len'];
          $cck_field['scale'] = $field['prec'];
        }
        elseif (in_array($field['data_type'], array('Date', 'DateTime'))) {
          $cck_field['type'] = 'datestamp';
          $cck_field['widget_type'] = 'date_text';
          $cck_field['widget_settings'] = array(
            'input_format' => 'm/d/Y - H:i:s',
          ); // @todo: Format this according to Date module defaults?
          $cck_field['year_range'] = '-0:+1';
        }
        else {
          // Assume all others are Character
          $cck_field['type'] = 'text';
          $cck_field['max_length'] = $field['max_len'];
          $cck_field['widget_type'] = $field['max_len'] > 127 ? 'text_textarea' : 'text_textfield';
        }
      }

      content_field_instance_create($cck_field);
      if (module_exists('fieldgroup')) {
        fieldgroup_update_fields($cck_field);
      }
      $record = array(
        'system_name' => $id,
        'cck' => 1,
      );
      drupal_set_message(t('Creating field %label (@id).', array('%label' => $cck_field['label'], '@id' => $id)));
      drupal_write_record('pirets_fields', $record, 'system_name');
    }
    // Else if the field is unchecked on the form but we ARE currently using it…
    elseif (!$form_state['values'][$id] && $field['cck']) {
      $label = db_query('SELECT label FROM {content_node_field_instance} WHERE field_name = :field_name && type_name = :type_name', array(':field_name' => 'pirets_' . $id, ':type_name' => 'pirets_prop'))->fetchField();
      drupal_set_message(t('Removing field %label (@id).', array('%label' => $label, '@id' => $id)));
      content_field_instance_delete('pirets_' . $id, 'pirets_prop');
      // If that field was the image count field, delete the image field too.
      if ($field['correlation'] === 'pic_count') {
        drupal_set_message(t('Removing <em>Images</em> field.'));
        content_field_instance_delete('pirets_images', 'pirets_prop');
      }
      $record = array(
        'system_name' => $id,
        'cck' => 0,
      );
      drupal_write_record('pirets_fields', $record, 'system_name');
    }
  }
  drupal_set_message(t('The configuration options have been saved.'));
  cache_clear_all('pirets_fields', 'cache');
  cache_clear_all('pirets_fields_active', 'cache');
}

/**
 * Allow the user to specify special correlations for particular fields.
 */
function pirets_sets_fmanage_form($form, &$form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  $form = array(
    'fields' => array(
      '#weight' => 10,
      '#theme' => 'pirets_fmanage_form',
      'rows' => array(),
    ),
    'use_img' => array(
      '#type' => 'checkbox',
      '#title' => t('Add a CCK ImageField to property nodes to store property images'),
      '#weight' => 20,
    ),
    'use_loc' => array(
      '#type' => 'checkbox',
      '#title' => t('Add a CCK Location field to property nodes to store address information'),
      '#weight' => 30,
    ),
    'submit' => array(
      '#type' => 'submit',
      '#value' => t('Save configuration'),
      '#weight' => 40,
    ),
  );
  $weight = 0;
  $corrs = array(
    'none' => t('None'),
    'mls_id' => t('MLS ID number'),
    'body' => t('Description (node body)'),
    'pic_count' => t('Picture count'),
    'sale_status' => t('Sale status'),
    'price' => t('Price'),
    'loc_street_1' => t('Location: Street address 1'),
    'loc_street_2' => t('Location: Street address 2'),
    'loc_street_3' => t('Location: Street address 3'),
    'loc_street_4' => t('Location: Street address 4'),
    'loc_street_5' => t('Location: Street address 5'),
    'loc_street_6' => t('Location: Street address 6'),
    'loc_city' => t('Location: City'),
    'loc_province' => t('Location: State'),
    'loc_postal_code' => t('Location: ZIP code'),
  );
  $fields = pirets_fields_active_fetch();
  foreach (pirets_fields_active_fetch() as $field) {
    $form['fields']['rows'][$field['system_name']] = array(
      "label_{$field['system_name']}" => array(
        '#type' => 'item',
        '#value' => is_numeric($field['system_name']) ? t('@sysname (@longname) <span class="error">(Numeric field name!)</span>', array('@sysname' => $field['system_name'], '@longname' => $field['long_name'])) : t('@sysname (@longname)', array('@sysname' => $field['system_name'], '@longname' => $field['long_name'])),
        '#weight' => $weight += 10,
      ),
      "classes_{$field['system_name']}" => array(
        '#type' => 'item',
        '#value' => implode(', ', array_map('check_plain', explode(',', $field['classes']))),
        '#weight' => $weight += 10,
      ),
      "corr_{$field['system_name']}" => array(
        '#type' => 'select',
        '#options' => $corrs,
        '#default_value' => $field['correlation'] === '' ? 'none' : $field['correlation'],
        '#weight' => $weight += 10,
      ),
    );
  }
  // Can we use an ImageField to store property images?
  if (module_exists('imagefield')) {
    $form['use_img']['#default_value'] = variable_get('pirets_use_img', FALSE);
    $form['use_img']['#description'] = t('Images will be automatically added to this field if you correlate a <em>Picture count</em> field above.');
  }
  else {
    $form['use_img']['#disabled'] = TRUE;
    $form['use_img']['#description'] = t('You must install the <em>ImageField</em> module to use this feature.');
  }
  // Can we use a Location field to store property location info?
  if (module_exists('location_cck')) {
    $form['use_loc']['#default_value'] = variable_get('pirets_use_loc', FALSE);
    $form['use_loc']['#description'] = t('Location data will be added to this field according to the <em>Location</em> fields correlated above. If your RETS provider uses several separate fields to store the street address, correlate each field in order (<em>Location: Street address 1</em>, <em>Location: Street address 2</em>, etc) and the values will be concatenated into a single string when stored.');
  }
  else {
    $form['use_loc']['#disabled'] = TRUE;
    $form['use_loc']['#description'] = t('You must install the <em>Location CCK</em> module to use this feature.');
  }
  return $form;
}

/**
 * Validator for the field management form.
 */
function pirets_sets_fmanage_form_validate($form, $form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  // Check for dupes
  $corrs = array();
  foreach (array_keys(pirets_fields_active_fetch()) as $field) {
    $corrfield = 'corr_' . $field;
    if ($form_state['values'][$corrfield] !== 'none') {
      if (in_array($form_state['values'][$corrfield], $corrs)) {
        form_set_error($corrfield, t('%corr is correlated to more than one field.', array('%corr' => $form['fields']['rows'][$field]['corr_' . $field]['#options'][$form_state['values'][$corrfield]])));
      }
      else {
        $corrs[] = $form_state['values'][$corrfield];
      }
    }
  }
  // Check for lack of required values
  if (!in_array('mls_id', $corrs)) {
    form_set_error('_mls_corr', t('A correlation to <em>MLS ID number</em> is required.'));
  }
  if (!in_array('sale_status', $corrs)) {
    form_set_error('_sale_corr', t('A correlation to <em>Sale status</em> is required.'));
  }
}

/**
 * Submit function for the field management form.
 */
function pirets_sets_fmanage_form_submit($form, $form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  foreach (pirets_fields_active_fetch() as $field) {
    $corrfield = 'corr_' . $field['system_name'];
    if ($form_state['values'][$corrfield] === 'none') {
      $form_state['values'][$corrfield] = '';
    }
    if ($form_state['values'][$corrfield] !== $field['correlation']) {
      $record = array(
        'system_name' => $field['system_name'],
        'correlation' => $form_state['values'][$corrfield],
      );
      drupal_write_record('pirets_fields', $record, 'system_name');
    }
  }

  $use_img = variable_get('pirets_use_img', FALSE);
  if ($form_state['values']['use_img'] && !$use_img) {
    // Create an ImageField and add it to the property content type
    $img = array(
      'field_name' => 'pirets_images',
      'type_name' => 'pirets_prop',
      'label' => t('Property images'),
      'type' => 'filefield',
      'widget_type' => 'imagefield_widget',
      'file_extensions' => 'gif jpg jpeg png',
      'custom_alt' => FALSE,
      'custom_title' => FALSE,
      'group' => 'group_pirets',
      'multiple' => TRUE,
      'weight' => -10,
    );
    module_load_include('inc', 'content', 'includes/content.crud');
    drupal_set_message(t('Creating <em>Property images</em> field.'));
    content_field_instance_create($img);
    if (module_exists('fieldgroup')) {
      fieldgroup_update_fields($img);
    }
    variable_set('pirets_use_img', TRUE);
  }
  elseif (!$form_state['values']['use_img'] && $use_img) {
    // Remove the ImageField from the property content type
    module_load_include('inc', 'content', 'includes/content.crud');
    drupal_set_message(t('Removing <em>Property images</em> field.'));
    content_field_instance_delete('pirets_images', 'pirets_prop');
    variable_set('pirets_use_img', FALSE);
  }

  $use_loc = variable_get('pirets_use_loc', FALSE);
  if ($form_state['values']['use_loc'] && !$use_loc) {
    // Create a Location field and add it to the property content type
    //
    $loc = array(
      'field_name' => 'pirets_loc',
      'type_name' => 'pirets_prop',
      'display_settings' => array(
        'label' => array(
          'format' => 'above',
          'exclude' => 0,
        ),
        'teaser' => array(
          'format' => 'default',
          'exclude' => 0,
        ),
        'full' => array(
          'format' => 'default',
          'exclude' => 0,
        ),
        4 => array(
          'format' => 'default',
          'exclude' => 0,
        ),
      ),
      'widget_active' => '1',
      'type' => 'location',
      'required' => '0',
      'multiple' => '0',
      'db_storage' => '1',
      'module' => 'location_cck',
      'active' => '1',
      'locked' => '0',
      'columns' => array(
        'lid' => array(
          'type' => 'int',
          'unsigned' => true,
          'not null' => false,
        ),
      ),
      'location_settings' => array(
        'form' => array(
          'fields' => array(
            'name' => array(
              'collect' => '0',
              'default' => '',
              'weight' => '2',
            ),
            'street' => array(
              'collect' => '1',
              'default' => '',
              'weight' => '4',
            ),
            'additional' => array(
              'collect' => '0',
              'default' => '',
              'weight' => '6',
            ),
            'city' => array(
              'collect' => '0',
              'default' => '',
              'weight' => '8',
            ),
            'province' => array(
              'collect' => '1',
              'default' => '',
              'weight' => '10',
            ),
            'postal_code' => array(
              'collect' => '1',
              'default' => '',
              'weight' => '12',
            ),
            'country' => array(
              'collect' => '4',
              'default' => 'us',
              'weight' => '14',
            ),
            'locpick' => array(
              'collect' => '1',
              'weight' => '20',
            ),
          ),
        ),
        'display' => array(
          'hide' => array(
            'name' => 0,
            'street' => 0,
            'additional' => 0,
            'city' => 0,
            'province' => 0,
            'postal_code' => 0,
            'country' => 0,
            'locpick' => 0,
            'province_name' => 0,
            'country_name' => 0,
            'map_link' => 0,
            'coords' => 0,
          ),
        ),
      ),
      'widget' => array(
        'default_value' => array(
          0 => array(
            'name' => '',
            'street' => '',
            'additional' => '',
            'country' => 'us',
            'locpick' => array(
              'user_latitude' => '',
              'user_longitude' => '',
            ),
            'cck_preview_in_progress' => true,
            'location_settings' => array(
              'form' => array(
                'fields' => array(
                  'name' => array(
                    'default' => '',
                    'collect' => '1',
                    'weight' => '2',
                  ),
                  'street' => array(
                    'default' => '',
                    'collect' => '1',
                    'weight' => '4',
                  ),
                  'additional' => array(
                    'default' => '',
                    'collect' => '0',
                    'weight' => '6',
                  ),
                  'city' => array(
                    'default' => '',
                    'collect' => '0',
                    'weight' => '8',
                  ),
                  'province' => array(
                    'default' => '',
                    'collect' => '0',
                    'weight' => '10',
                  ),
                  'postal_code' => array(
                    'default' => '',
                    'collect' => '0',
                    'weight' => '12',
                  ),
                  'country' => array(
                    'default' => 'us',
                    'collect' => '1',
                    'weight' => '14',
                  ),
                  'locpick' => array(
                    'default' => array(
                      'user_latitude' => '',
                      'user_longitude' => '',
                    ),
                    'collect' => '1',
                    'weight' => '20',
                    'nodiff' => true,
                  ),
                  'cck_preview_in_progress' => array(
                    'default' => true,
                  ),
                  'location_settings' => array(
                    'default' => array(
                      'form' => array(
                        'fields' => array(
                          'lid' => array(
                            'default' => false,
                          ),
                          'name' => array(
                            'default' => '',
                            'collect' => 1,
                            'weight' => 2,
                          ),
                          'street' => array(
                            'default' => '',
                            'collect' => 1,
                            'weight' => 4,
                          ),
                          'additional' => array(
                            'default' => '',
                            'collect' => 1,
                            'weight' => 6,
                          ),
                          'city' => array(
                            'default' => '',
                            'collect' => 0,
                            'weight' => 8,
                          ),
                          'province' => array(
                            'default' => '',
                            'collect' => 0,
                            'weight' => 10,
                          ),
                          'postal_code' => array(
                            'default' => '',
                            'collect' => 0,
                            'weight' => 12,
                          ),
                          'country' => array(
                            'default' => 'us',
                            'collect' => 1,
                            'weight' => 14,
                          ),
                          'locpick' => array(
                            'default' => false,
                            'collect' => 1,
                            'weight' => 20,
                            'nodiff' => true,
                          ),
                          'latitude' => array(
                            'default' => 0,
                          ),
                          'longitude' => array(
                            'default' => 0,
                          ),
                          'source' => array(
                            'default' => 0,
                          ),
                          'is_primary' => array(
                            'default' => 0,
                          ),
                          'delete_location' => array(
                            'default' => false,
                            'nodiff' => true,
                          ),
                        ),
                      ),
                    ),
                  ),
                  'lid' => array(
                    'default' => false,
                  ),
                  'latitude' => array(
                    'default' => 0,
                  ),
                  'longitude' => array(
                    'default' => 0,
                  ),
                  'source' => array(
                    'default' => 0,
                  ),
                  'is_primary' => array(
                    'default' => 0,
                  ),
                  'delete_location' => array(
                    'default' => false,
                    'nodiff' => true,
                  ),
                ),
              ),
              'display' => array(
                'hide' => array(
                  'name' => 0,
                  'street' => 0,
                  'additional' => 0,
                  'city' => 0,
                  'province' => 0,
                  'postal_code' => 0,
                  'country' => 0,
                  'locpick' => 0,
                  'province_name' => 0,
                  'country_name' => 0,
                  'map_link' => 0,
                  'coords' => 0,
                ),
              ),
            ),
          ),
        ),
        'default_value_php' => NULL,
        'label' => t('Location'),
        'weight' => '51',
        'description' => '',
        'type' => 'location',
        'module' => 'location_cck',
      ),
    );

    module_load_include('inc', 'content', 'includes/content.crud');
    drupal_set_message(t('Creating <em>Location</em> field.'));
    content_field_instance_create($loc);
    if (module_exists('fieldgroup')) {
      fieldgroup_update_fields($loc);
    }
    variable_set('pirets_use_loc', TRUE);
  }
  elseif (!$form_state['values']['use_loc'] && $use_loc) {
    // Remove the Location field from the property content type
    module_load_include('inc', 'content', 'includes/content.crud');
    drupal_set_message(t('Removing <em>Location</em> field.'));
    content_field_instance_delete('pirets_loc', 'pirets_prop');
    variable_set('pirets_use_loc', FALSE);
  }

  drupal_set_message(t('The configuration options have been saved.'));
  cache_clear_all('pirets_fields', 'cache');
  cache_clear_all('pirets_fields_active', 'cache');
}

/**
 * Options to limit what properties are fetched from the RETS server.
 */
function pirets_sets_limits_form($form, &$form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  // Find the sale status field
  $stat_field = FALSE;

  foreach (pirets_fields_active_fetch() as $field) {
    if ($field['correlation'] === 'sale_status') {
      $stat_field = $field;
      continue;
    }
  }
  if ($stat_field === FALSE) {
    drupal_set_message(t('You must correlate a sale status field first.'), 'error');
    drupal_goto('admin/config/pirets/fields');
  }

  $is_active = '';
  $opts = array();
  foreach (pirets_lookups_fetch($stat_field['lookup']) as $lup) {
    if ($lup['long_value'] === 'Active') {
      $is_active = $lup['value'];
    }
    $opts[$lup['value']] = check_plain($lup['long_value']);
  }

  $form = array(
    'pirets_limit_sstatuses' => array(
      '#type' => 'select',
      '#title' => t('Sale statuses'),
      '#weight' => 10,
      '#required' => TRUE,
      '#options' => $opts,
      '#default_value' => variable_get('pirets_limit_sstatuses', array($is_active)),
      '#description' => t('PIRETS will only fetch listings which have this sale status value(s). If in doubt, you probably only want to select the one that says &ldquo;Active&rdquo; or something similar.'),
      '#multiple' => TRUE,
      '#weight' => 0,
    ),
    'pirets_chunk' => array(
      '#title' => t('Record chunk size'),
      '#type' => 'select',
      '#options' => drupal_map_assoc(array(250, 500, 1000, 1500, 2000, 3000)) + array('0' => t('Unlimited')),
      '#default_value' => variable_get('pirets_chunk', '2000'),
      '#description' => t('When PIRETS processes incoming records, it will work with data structures containing this many records simultaneously, and cache the rest for later. If updating listings is often failing due to out of memory errors and increasing PHP&rsquo;s memory allocation is not an option, try decreasing this value. However, decreasing this value increases the amount of time it will take to update listings. The RETS server may override this value (MAXROWS).'),
      '#weight' => 10,
    ),
  );

  $form = system_settings_form($form);
  unset($form['buttons']['reset']);
  $form['buttons']['#weight'] = 100;
  return $form;
}

/**
 * Display classes and allow users to force update properties in the classes.
 */
function pirets_sets_fupdate_form($form, &$form_state) {
  module_load_include('inc', 'pirets', 'pirets.connect');
  // Check for required correlations first
  $mls_field = NULL;
  $status_field = NULL;

  $fields = pirets_fields_active_fetch();
  foreach (array_keys($fields) as $field) {
    if ($fields[$field]['correlation'] === 'mls_id') {
      $mls_field = $fields[$field]['system_name'];
    }
    elseif ($fields[$field]['correlation'] === 'sale_status') {
      $status_field = $fields[$field]['system_name'];
    }
  }

  // Check to see we have fields we need
  if ($mls_field === NULL || $status_field === NULL) {
    drupal_set_message(t('Properties cannot be updated until all required field correlations have been set.'), 'error');
    drupal_goto('admin/config/pirets/fields');
  }

  $form = array();
  $weight = -10;
  foreach (pirets_classes_fetch() as $key => $class) {
    if ($class['enabled']) {
      $count = db_query('SELECT COUNT(DISTINCT(nid)) FROM {content_type_pirets_prop} WHERE pirets_class_value = :pirets_class_value', array(':pirets_class_value' => $key))->fetchField();
      $form['update_' . $key] = array(
        '#type' => 'item',
        '#title' => t('@visible (@class)', array('@visible' => $class['visible'], '@class' => $key)),
        '#description' => t('%count in database; last updated %update', array('%count' => $count, '%update' => $class['last_update'] ? format_date($class['last_update']) : t('(never)'))),
        '#value' => theme('links', array('links' => array(
            array(
              'title' => t('Update now'),
              'href' => 'admin/config/pirets/update/batch/' . $key,
            ),
            array(
              'title' => t('Flush'),
              'href' => 'admin/config/pirets/flush/' . $key,
            ),
          ))),
        '#weight' => $weight += 10,
      );
    }
  }
  $form['flush_all'] = array(
    '#type' => 'item',
    '#title' => t('Flush all'),
    '#value' => l(t('Flush all'), 'admin/config/pirets/flush'),
    '#weight' => $weight += 10,
  );
  return $form;
}

/**
 * Confirm flushing of listings. drupal_get_form() callback.
 */
function pirets_flush($form, $class = NULL) {
  return confirm_form(array(
    'class' => array(
      '#type' => 'value',
      '#value' => $class,
    ),
  ), $class === NULL ? t('Are you sure you want to delete all property listings?') : t('Are you sure you want to delete all property listings in the %class class?', array('%class' => $class)), 'admin/config/pirets/update', t('The listings will be deleted from this server, but the listings on the RETS server will not be modified and may be re-fetched if they still exist. This operation may take a significant amount of time if there are many listings to delete; please be patient.'));
}

/**
 * Submission function for pirets_flush().
 *
 * Note that, unlike for listing updating, we're not limiting ourselves to
 * enabled property classes here. I don't think that will be a problem… will it?
 * @see pirets_class_update_batch()
 */
function pirets_flush_submit($form, &$form_state) {
  // Get the nids of the nodes to be deleted.
  if ($form_state['values']['class'] === NULL) {
    $rez = db_query('SELECT nid FROM {node} WHERE type = :type', array(':type' => "pirets_prop"));
    $batch_title = t('Deleting all property listings.');
  }
  else {
    $rez = db_query('SELECT DISTINCT(nid) FROM {content_type_pirets_prop} WHERE pirets_class_value = :pirets_class_value', array(':pirets_class_value' => $form_state['values']['class']));
    $batch_title = t('Deleting all property listings in the %class class.', array('%class' => $form_state['values']['class']));
  }
  $nids = array();
  while ($nid = db_fetch_object($rez)) {
    $nids[] = $nid->nid;
  }
  batch_set(array(
    'title' => $batch_title,
    'operations' => array(
      array('pirets_flush_batch', array($nids, $form_state['values']['class'])),
    ),
    'finished' => 'pirets_flush_batch_finished',
    'file' => drupal_get_path('module', 'pirets') . '/pirets.batch.inc',
  ));
}

/**
 * Themes the class elements into a nice table.
 */
function theme_pirets_classes_form($variables) {
  $form = $variables['form'];
  $rows = array();
  foreach ($form['class_names']['#value'] as $sys) {
    // Title already in the column header; no need to re-show it.
    unset($form[$sys][$sys . '_visible']['#title']);
    unset($form[$sys][$sys . '_enabled']['#title']);
    // TODO Please change this theme call to use an associative array for the $variables parameter.
    $rows[] = array(
      theme('item', $form[$sys][$sys . '_class']),
      theme('textfield', $form[$sys][$sys . '_visible']),
      theme('checkbox', $form[$sys][$sys . '_enabled']),
      theme('select', $form[$sys][$sys . '_lifetime']),
    );
  }
  // TODO Please change this theme call to use an associative array for the $variables parameter.
  return theme(
    'table',
    array(t('RETS class name'), t('Visible name'), t('Enabled'), t('Lifetime')),
    $rows
  );
}

/**
 * Themes the field correlation form into a nice table.
 */
function theme_pirets_fmanage_form($variables) {
  $form = $variables['form'];
  $rows = array();
  foreach (element_children($form['rows']) as $field) {
    $row = array();
    foreach (element_children($form['rows'][$field]) as $column) {
      // TODO Please change this theme call to use an associative array for the $variables parameter.
      $row[] = theme($form['rows'][$field][$column]['#type'], $form['rows'][$field][$column]);
    }
    $rows[] = $row;
  }
  return theme('table', array('header' => array(t('Field'), t('Prop types'), t('Correlation')), 'rows' => $rows));
}
