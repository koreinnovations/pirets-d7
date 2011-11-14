<?php

/**
 * @file
 * Various batch operations related to listing fetching, updating and deletion.
 */

module_load_include('inc', 'pirets', 'pirets.connect');

/**
 * Set up the batch job and initialize it.
 */
function pirets_class_update_batch($class, $progressive = TRUE) {
  $classes = pirets_classes_fetch();
  if (isset($classes[$class]) && $classes[$class]['enabled']) {
    batch_set(array(
      'title' => t('Updating properties in class @class (@visible)', array('@class' => $class, '@visible' => $classes[$class]['visible'])),
      'operations' => array(
        array('pirets_class_update_batch_fetch', array($class)),
        array('pirets_class_update_batch_update', array()),
        array('pirets_class_update_batch_del', array()),
      ),
      'finished' => 'pirets_class_update_batch_finished',
      'file' => './' . drupal_get_path('module', 'pirets') . '/pirets.batch.inc',
    ));
    // http://drupal.org/node/638712
    $batch = &batch_get();
    $batch['progressive'] = $progressive;
    batch_process('admin/config/pirets/update');
  }
  else {
    drupal_not_found();
    return;
  }
}

/**
 * Fetch the listings from the RETS server. Batch callback.
 */
function pirets_class_update_batch_fetch($class, &$context) {
  // Setup if this is the first time running
  if (!count($context['sandbox'])) {
    $context['sandbox']['offset'] = 0;
    $context['sandbox']['props'] = array();
    $context['results']['ask_fields'] = array();
    $context['results']['class'] = $class;
    $context['results']['error'] = FALSE;
    $context['results']['counts'] = array(
      'checked' => 0,
      'new' => 0,
      'update' => 0,
      'del' => 0,
      'keep' => 0,
    );

    // Build a list of fields we're asking for.
    $context['results']['fields'] = pirets_fields_active_fetch();
    // foreach ($fields as &$field) {
    // This effs up the last element of $fields for later iteration through it,
    // so let's do it ghetto-style.
    foreach (array_keys($context['results']['fields']) as $field) {
      $context['results']['fields'][$field]['classes'] = explode(',', $context['results']['fields'][$field]['classes']);
      if (in_array($class, $context['results']['fields'][$field]['classes'])) {
        $context['results']['ask_fields'][] = $field;
        if ($context['results']['fields'][$field]['correlation'] === 'mls_id') {
          $context['results']['mls_field'] = $context['results']['fields'][$field]['system_name'];
        }
        elseif ($context['results']['fields'][$field]['correlation'] === 'sale_status') {
          $status_field = $context['results']['fields'][$field]['system_name'];
        }
        elseif ($context['results']['fields'][$field]['correlation'] === 'price') {
          $price_field = $context['results']['fields'][$field]['system_name'];
        }
      }
    }

    // Build the query.
    $statuses = variable_get('pirets_limit_sstatuses', array());
    // Rappatoni wants us to use the "or" operator, even if there's just one
    // status. Other servers seem okay with this.
    $statuses_q = '|' . implode(',', $statuses);
    $context['sandbox']['limit'] = variable_get('pirets_chunk', 2000);
    if ($context['sandbox']['limit'] == 0) {
      $context['sandbox']['limit'] = 'NONE';
    }
    $context['results']['chunks'] = 0;
    $context['sandbox']['query'] = array("{$status_field}={$statuses_q}");
    if (isset($price_field)) {
      $context['sandbox']['query'][] = "{$price_field}=0+";
    }
  }

  $response = pirets_query_search($class, $context['sandbox']['query'], $context['results']['ask_fields'], $context['sandbox']['limit'], $context['sandbox']['offset']);
  if ($response->response_code === 200) {
    $chunk = $response->data;
    cache_set('pirets_chunk_' . $context['results']['chunks']++, $chunk);
    $context['sandbox']['offset'] += count($chunk);
  }
  else {
    $context['results']['error'] = TRUE;
    $context['finished'] = 1;
    return;
  }

  $context['finished'] = strpos($response->data_raw, '<MAXROWS') === FALSE;
  $context['message'] = t('@count listings fetched from RETS server.', array('@count' => $context['sandbox']['offset']));
  $context['results']['counts']['total'] = $context['sandbox']['offset'];
}

/**
 * Insert new listings; update current ones. Batch callback.
 */
function pirets_class_update_batch_update(&$context) {
  if (!count($context['sandbox'])) {
    if ($context['results']['error']) {
      $finished = 1;
      return;
    }
    // Load the info for the properties which already belong to
    // this class.
    $context['results']['db_props'] = array();
    // TODO Please convert this statement to the D7 database API syntax.
    $rez = db_query("SELECT nid, pirets_crc32_value, pirets_{$context['results']['mls_field']}_value AS mls FROM {content_type_pirets_prop} WHERE pirets_class_value = \"%s\"", $context['results']['class']);
    while ($prop = db_fetch_array($rez)) {
      $prop = array_map('intval', $prop);
      $context['results']['db_props'][$prop['mls']] = $prop;
    }
    $context['results']['in_rets'] = array();
    if ($context['results']['counts']['total'] === 0) {
      // We don't have any to update, but we might have some to delete…
      return;
    }
    $context['sandbox']['chunk_idx'] = 0;
    $context['sandbox']['in_rets'] = array();
    $context['sandbox']['index'] = 0;
    $context['sandbox']['img_dir'] = file_directory_path() . '/pirets_img/';
    if (variable_get('pirets_use_img', FALSE)) {
      $context['sandbox']['placeholder'] = array(
        // Most of these fields are probably unnecessary…?
        'title' => t('PIRETS property image placeholder'),
        'filesize' => 34,
        'mimetype' => 'image/jpeg',
        'description' => '',
        'list' => TRUE,
        'data' => array(
          'alt' => t('PIRETS property image placeholder'),
          'title' => t('PIRETS property image placeholder'),
        ),
      );
      $context['sandbox']['file'] = new stdClass();
      $context['sandbox']['file']->filemime = 'image/jpeg';
      $context['sandbox']['file']->filesize = 0;
      $context['sandbox']['file']->uid = 0;
      $context['sandbox']['file']->status = FILE_STATUS_PERMANENT;
      $context['sandbox']['file']->timestamp = REQUEST_TIME;
      $context['sandbox']['file']->filepath = drupal_get_path('module', 'pirets') . '/img/pirets_blank.jpeg';
    }
    else {
      $context['sandbox']['file'] = FALSE;
    }
  }

  // We're going to define some pointers because the code below gets *really*
  // ugly when we use the full array names.
  $_index = &$context['sandbox']['index'];
  $_counts = &$context['results']['counts'];
  $_mls_field = &$context['results']['mls_field'];
  $_in_rets = &$context['results']['in_rets'];
  $_db_props = &$context['results']['db_props'];
  $_fields = &$context['results']['fields'];
  $_file = &$context['sandbox']['file'];

  $img_dir = file_directory_path() . '/pirets_img';
  $chunk_name = 'pirets_chunk_' . $context['sandbox']['chunk_idx'];
  $_props = cache_get($chunk_name);
  $chunk_count = count($_props->data);
  $context['finished'] = 0;

  // Batches of 50
  for ($x = 0; $x < 50 && $_index < $chunk_count && $context['finished'] < 1; $x++) {
    $prop = $_props->data[$_index];
    $_in_rets[] = $prop[$_mls_field];
    // If this property is not currently in the database OR its hash has changed…
    if (!isset($_db_props[$prop[$_mls_field]]) || $_db_props[$prop[$_mls_field]]['pirets_crc32_value'] != intval($prop['crc32'])) {
      if (isset($_db_props[$prop[$_mls_field]])) {
        // It's a currently-existing node, so load it.
        // The third param tells node_load() to reset the loaded node cache,
        // reducing memory usage.
        $node = node_load($_db_props[$prop[$_mls_field]]['nid'], NULL, TRUE);
        $new = FALSE;
        $_counts['update']++;
      }
      else {
        // It's not in the database, so let's create a new node.
        $node = new stdClass();
        $node->type = 'pirets_prop';
        $node->title = $prop[$_mls_field];
        $new = TRUE;
        $_counts['new']++;
      }
      // Now edit the node.
      $loc_data = array(
        'street' => array(),
      );
      foreach ($context['results']['ask_fields'] as $field) {
        // Convert the value if necessary.
        if ($_fields[$field]['data_type'] === 'Date') {
          preg_match('/^(\d{4})-(\d{2})-(\d{2})$/', $prop[$field], $matches);
          $value = gmmktime(0, 0, 0, $matches[2], $matches[3], $matches[1]);
        }
        elseif ($_fields[$field]['data_type'] === 'DateTime') {
          preg_match('/^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})$/', $prop[$field], $matches);
          $value = gmmktime($matches[4], $matches[5], $matches[6], $matches[2], $matches[3], $matches[1]);
        }
        elseif ($_fields[$field]['lup_multi']) {
          $value = explode(',', $prop[$field]);
        }
        else {
          $value = $prop[$field];
        }
        $node_child = 'pirets_' . $field;
        if (is_array($value)) {
          $cck_value = array();
          foreach ($value as $val) {
            $cck_value[] = array('value' => $val);
          }
          $node->{$node_child} = $cck_value;
        }
        else {
          $node->{$node_child} = array(
            array(
              'value' => $value,
            ),
          );
        }
        // Should we use this for the body field?
        if ($_fields[$field]['correlation'] === 'body') {
          $node->body = $value;
        }
        elseif ($_fields[$field]['correlation'] === 'pic_count' && $_file/*  && $_placeholder */) {
          $node->pirets_images = array();
          // Attach the placeholder image
          $prop_dir = "{$img_dir}/{$prop[$_mls_field]}";
          if (!is_dir($prop_dir) && !mkdir($prop_dir, 0755)) {
            watchdog('pirets', 'Directory @dir doesn\'t exist and couldn\'t be created (probably a permissions problem).', array('@dir' => $prop_dir), WATCHDOG_ERROR);
          }
          else {
            for ($index = 1; $index <= $value; $index++) {
              $fname = "{$prop[$_mls_field]}-{$index}.jpeg";
              $dest = "{$prop_dir}/{$fname}";
              // Since file_copy stupidly doesn't let us specify the destination
              // path explicitly, stopping us from copying a file so it has a
              // different filename than the source, we do this workaround.
              $_file->filename = $fname;
              $fclone = clone $_file;
              if (file_copy($fclone, $prop_dir, FILE_EXISTS_REPLACE)) {
                $_file->filepath = $dest;
                drupal_write_record('files', $_file);
                $_placeholder['fid'] = $_file->fid;
                $_placeholder['filename'] = $fname;
                $_placeholder['filepath'] = $dest;
                $node->pirets_images[] = $_placeholder;
              }
              else {
                watchdog('pirets', 'File @file couldn\'t be saved (probably a permissions problem).', array('@file' => $dest), WATCHDOG_ERROR);
              }
            }
          }
        }
        elseif (strpos($_fields[$field]['correlation'], 'loc_') === 0) {
          if (preg_match('/^loc_street_(\d+)$/', $_fields[$field]['correlation'], $matches)) {
            if ($value !== '') {
              $loc['street'][intval($matches[1])] = $value;
            }
          }
          else {
            $loc[$_fields[$field]['correlation']] = $value;
          }
        }
      }
      if (variable_get('pirets_use_loc', FALSE)) {
        // Build values for Location field
        $node->pirets_loc = array(
          array(),
        );
        foreach (array('postal_code', 'province', 'city') as $field) {
          $loc_field = 'loc_' . $field;
          if (isset($loc[$loc_field])) {
            $node->pirets_loc[0][$field] = $loc[$loc_field];
          }
        }
        if (count($loc['street'])) {
          $node->pirets_loc[0]['street'] = implode(' ', $loc['street']);
        }
      }
      $node->pirets_class = array(
        array(
          'value' => $context['results']['class'],
        ),
      );
      $node->pirets_crc32 = array(
        array(
          'value' => $prop['crc32'],
        ),
      );
      $node = node_submit($node);
      $node->revision = TRUE;
      if ($new) {
        watchdog('pirets', 'Creating node @nid (@title). MLS ID: @mls', array('@nid' => $node->nid, '@title' => $node->title, '@mls' => $prop[$context['results']['mls_field']]));
      }
      else {
        watchdog('pirets', 'Updating node @nid (@title). MLS ID: @mls', array('@nid' => $node->nid, '@title' => $node->title, '@mls' => $prop[$context['results']['mls_field']]));
      }
      node_save($node);
      // Clear the $_SESSION['messages'] to keep it from taking up a bunch of
      // memory
      unset($_SESSION['messages']);
    }
    // Else, leave the node alone
    else {
      $_counts['keep']++;
    }
    $context['finished'] = ++$_counts['checked'] / $_counts['total'];
    if (++$_index >= $chunk_count) {
      // Prepare for next chunk;
      $_index = 0;
      // Remove this chunk from cache;
      cache_clear_all($chunk_name, 'cache');
      $context['sandbox']['chunk_idx']++;
    }
  } // End of for loop
  $context['message'] = t('Processed @index of @count (@new created, @update updated)', array('@index' => $_counts['checked'], '@count' => $_counts['total'], '@new' => $_counts['new'], '@update' => $_counts['update']));
}

/**
 * Delete old listings. Batch callback.
 */
function pirets_class_update_batch_del(&$context) {
  if (!count($context['sandbox'])) {
    if ($context['results']['error']) {
      $finished = 1;
      return;
    }
    // Both of these arrays contain MLS numbers.
    $context['sandbox']['to_delete'] = array_values(array_diff(array_keys($context['results']['db_props']), $context['results']['in_rets']));
    $context['sandbox']['to_delete_count'] = count($context['sandbox']['to_delete']);
    if ($context['sandbox']['to_delete_count'] === 0) {
      return;
    }
    $context['sandbox']['index'] = 0;
  }
  $context['finished'] = 0;
  for ($x = 0; $x < 25 && $context['finished'] < 1; $x++) {
    $node = node_load($context['results']['db_props'][$context['sandbox']['to_delete'][$context['sandbox']['index']]]['nid'], NULL, TRUE);
    node_delete($node->nid);
    unset($_SESSION['messages']);
    $context['finished'] = ++$context['sandbox']['index'] / $context['sandbox']['to_delete_count'];
  }
  $context['results']['counts']['del'] = $context['sandbox']['index'];
  $context['message'] = t('Deleted @index of @count.', array('@index' => $context['sandbox']['index'], '@count' => $context['sandbox']['to_delete_count']));
}

/**
 * Display batch processing result to user. Batch callback.
 */
function pirets_class_update_batch_finished($success, $results, $operations) {
  if (count($results) && $results['counts']['checked'] >= $results['counts']['total']) {
    $classes = variable_get('pirets_classes', array());
    $classes[$results['class']]['last_update'] = REQUEST_TIME;
    variable_set('pirets_classes', $classes);
    if (variable_get('cron_semaphore', FALSE)) {
      watchdog('pirets', 'Class update for @class completed. @new created, @update updated, @del deleted, @keep unchanged.', array('@class' => $results['class'], '@new' => $results['counts']['new'], '@update' => $results['counts']['update'], '@del' => $results['counts']['del'], '@keep' => $results['counts']['keep']));
    }
    else {
      if ($results['error']) {
        drupal_set_message(t('Class update for @class did not finish due to a problem when connecting to the RETS server. See the <a href="!log">recent log entries page</a> for more information.', array('@class' => $results['class'], '!log' => url('admin/reports/dblog'))), 'error');
      }
      else {
        drupal_set_message(t('Class update for @class completed. @new created, @update updated, @del deleted, @keep unchanged.', array('@class' => $results['class'], '@new' => $results['counts']['new'], '@update' => $results['counts']['update'], '@del' => $results['counts']['del'], '@keep' => $results['counts']['keep'])));
      }
    }
  }
  else {
    drupal_set_message(t('It appears that class updating failed. If this was due to a PHP memory error, try increasing PHP&rsquo;s memory allocation (<em>memory_limit</em> value). If that is not an option, try decreasing the <em>Record chunk size</em> value in the <em>Advanced settings</em> section of the <a href="!slimits">Search limits</a> configuration page. If this was due to a PHP timeout error, try increasing PHP&rsquo;s execution time (<em>max_execution_time</em> value). Update the class again to continue fetching listings for it.', array('!slimits' => url('admin/config/pirets/fields/limits'))));
  }
}

/**
 * Delete property listing nodes. Batch callback.
 */
function pirets_flush_batch($nids, $class, &$context) {
  if (!count($context['sandbox'])) {
    $context['sandbox']['count'] = count($nids);
    if ($context['sandbox']['count'] === 0) {
      return;
    }
    $context['sandbox']['index'] = 0;
    $context['sandbox']['nids'] = $nids;
    $context['results']['class'] = $class;
  }

  // $context['finished'] is set to 1 at the onset of each call to a batch
  // function, apparently. Set it to zero so that the for loop below will
  // properly run its first run.
  $context['finished'] = 0;

  for ($x = 0; $x < 25 && $context['finished'] < 1; $x++) {
    $node = node_load($context['sandbox']['nids'][$context['sandbox']['index']], NULL, TRUE);
    node_delete($node->nid);
    unset($_SESSION['messages']);
    $context['finished'] = ++$context['sandbox']['index'] / $context['sandbox']['count'];
  }
  $context['message'] = t('Deleted @index of @count.', array('@index' => $context['sandbox']['index'], '@count' => $context['sandbox']['count']));
}

/**
 * Display result to the user. Batch callback.
 */
function pirets_flush_batch_finished($success, $results, $operations) {
  // Set the last_update times for the relative classes back to 'never'
  $classes = variable_get('pirets_classes', array());
  if ($results['class'] === NULL) {
    foreach ($classes as &$class) {
      $class['last_update'] = 0;
    }
  }
  else {
    $classes[$results['class']]['last_update'] = 0;
  }
  variable_set('pirets_classes', $classes);
  drupal_set_message(t('The property listings have been deleted.'));
  drupal_goto('admin/config/pirets/update');
}
