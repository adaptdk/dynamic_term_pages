<?php

/**
 * @file
 * Handle the 'dynamic term view' task.
 */

/**
 * Specialized implementation of hook_page_manager_task_tasks(). See api-task.html for
 * more information.
 */
function dynamic_term_pages_dynamic_term_view_page_manager_tasks() {
  if (module_exists('taxonomy')) {
    return array(
      // This is a 'page' task and will fall under the page admin UI
      'task type' => 'page',
      'title' => t('Dynamic taxonomy term template'),
      'admin title' => t('Dynamic taxonomy term template'),
      'admin description' => t('Override dynamic path'),
      'admin path' => 'dtp/%/%',
      // Menu hooks so that we can alter the term/%term menu entry to point to us.
      'hook menu' => 'dynamic_term_pages_view_menu',
      'hook menu alter' => 'dynamic_term_pages_view_menu_alter',
      // Provide a setting to the primary settings UI for Panels
      'admin settings' => 'dynamic_term_pages_view_admin_settings',
      // Even though we don't have subtasks, this allows us to save our settings.
      'save subtask callback' => 'dynamic_term_pages_view_save',
      // Callback to add items to the page manager task administration form:
      'task admin' => 'dynamic_term_pages_view_task_admin',
      // This is task uses 'context' handlers and must implement these to give the
      // handler data it needs.
      'handler type' => 'context',
      'get arguments' => 'dynamic_term_pages_view_get_arguments',
      // Allow this to be enabled or disabled:
      'disabled' => variable_get('dynamic_term_pages_view_disabled', TRUE),
      'enable callback' => 'dynamic_term_pages_view_enable',
      // Allow additional operations
      'operations' => array(
        'settings' => array(
          'title' => t('Settings'),
          'description' => t('Edit name, path and other basic settings for the page.'),
          'form' => 'dynamic_term_pages_view_settings',
        ),
      ),
    );
  }
}

/**
 * Callback defined by page_manager_term_view_page_manager_tasks().
 *
 * Alter the term view input so that term view comes to us rather than the
 * normal term view process.
 */
function dynamic_term_pages_view_menu_alter(&$items, $task) {
  if (variable_get('dynamic_term_pages_view_disabled', TRUE)) {
    return;
  }

  // We need all possible paths, optional placeholders don't work
  $vocabs = dynamic_term_pages_get_enabled_vocabularies();
  $i = sizeof($vocabs);
  foreach ($vocabs as $k => $vocab) {
    $wildcards = array_fill(1, $i, '%dynamic_term_pages_term');
    $args = array_keys($wildcards);

    // Use a custom wildcard loader so that we can send in term objects loaded by url safe name
    $items['dtp/' . implode('/', $wildcards)] = array(
      'title' => 'Dynamic term page',
      'page callback' => 'dynamic_term_pages_view_page',
      'access callback' => 'user_access',
      'access arguments' => array('access content'),
      'file path' => $task['path'],
      'file' => $task['file'],
      'page arguments' => $args
    );

    $i--;
  }

  return $items;
}

/**
 * Entry point for our multiple terms view.
 */
function dynamic_term_pages_view_page() {

  $args = func_get_args();

  // Load my task plugin
  $task = page_manager_get_task('dynamic_term_view');

  // Load the term into a context.
  ctools_include('context');
  ctools_include('context-task-handler');

  $order = dynamic_term_pages_view_get_arguments($task, '');

  $ordered_args = array();

  // Populate an empty array of contexts keyed by vocabulary so we always have a fallback
  foreach ($order as $k => $v) {
    $ordered_args[$k] = 'empty';
  }

  foreach ($args as $arg) {
    // Look for a matching vocabulary and check if a context for this vocabulary has already been added, we only want one of each
    if (isset($ordered_args[$arg->vocabulary_machine_name])) {
      // Assumes parent if vocabulary is in the list.
      $ordered_args[$arg->vocabulary_machine_name] = $arg;
    }
  }

  // Create canonical url header since we serve the same content on multiple pages
  global $base_url;
  $parts = array();
  foreach ($ordered_args as $oarg) {
    // pathauto_name property gets attached in hook_taxonomy_term_load().
    if ($oarg != 'empty') {
      $parts[] = $oarg->pathauto_name;
    }
  }
  $canonical_url = $base_url . '/' . implode('/', $parts);

  drupal_add_html_head(array(
      '#tag' => 'link',
      '#attributes' => array(
        'href' => $canonical_url,
        'rel' => 'canonical',
      )
    ),
    'dynamic_term_canonical');

  $contexts = ctools_context_handler_get_task_contexts($task, '', $ordered_args);

  if (empty($contexts)) {
    return MENU_NOT_FOUND;
  }

  // Build the full output using the configured CTools plugin.
  $output = ctools_context_handler_render($task, '', $contexts, array('terms' => $args));
  if ($output !== FALSE) {
    return $output;
  }

  $build['no_content'] = array(
    '#prefix' => '<p>',
    '#markup' => t('There is currently no content classified with this term.'),
    '#suffix' => '</p>',
  );

  return $build;
}

/**
 * Callback to get arguments provided by this task handler.
 */
function dynamic_term_pages_view_get_arguments($task, $subtask_id) {
  $vocabs = dynamic_term_pages_get_enabled_vocabularies();
  $args = array();

  $i = 1;
  foreach ($vocabs as $k => $vocab) {
    $args[$k] = array(
      'keyword' => 'term_' . $k,
      'identifier' => $k,
      'id' => $i,
      'name' => 'term',
      'settings' => array('input_form' => 'term'),
      'default' => 'NULL',
    );
    $i++;
  }

  return $args;
}

/**
 * Settings page for this task.
 */
function dynamic_term_pages_view_settings($form, &$form_state) {
  $form['dynamic_term_pages_enabled_vocabularies']['#tree'] = TRUE;
  $enabled = dynamic_term_pages_get_enabled_vocabularies();
  $override_term_page = dynamic_term_pages_override_taxonomy_term_page();

  $vocabularies = taxonomy_get_vocabularies();
  foreach ($vocabularies as $vocab) {
    $options[$vocab->machine_name] = $vocab->name;

    $form['dynamic_term_pages_enabled_vocabularies'][$vocab->machine_name]['enabled'] = array(
      '#type' => 'checkbox',
      '#title' => $vocab->name,
      '#default_value' => isset($enabled[$vocab->machine_name]) ? $enabled[$vocab->machine_name]['enabled'] : '0',
      '#description' => t('Check to enable vocabulary and use weight to define canonical order of taxonomies.')
    );

    $form['dynamic_term_pages_enabled_vocabularies'][$vocab->machine_name]['weight'] = array(
      '#type' => 'weight',
      '#delta' => 20,
      '#default_value' => isset($enabled[$vocab->machine_name]) ? $enabled[$vocab->machine_name]['weight'] : '0',
    );
  }

  $form['dynamic_term_pages_override_taxonomy_term_page'] = array(
    '#type' => 'checkbox',
    '#title' => t('Override term page.'),
    '#description' => t('If enabled the dynamic term page will take over taxonomy/term/% for all enabled vocabulary term pages.'),
    '#default_value' => $override_term_page
  );

  $form['dynamic_term_pages_override_type'] = array(
    '#type' => 'radios',
    '#title' => t('Override type'),
    '#description' => t('Type of override will determine how the lookup is performed'),
    '#options' => array(0 => t('Default'), 1 => t('Hierarchical')),
    '#default_value' => variable_get('dynamic_term_pages_override_type', 0),
  );

  $settings = isset($form_state->update_values) ? $form_state->update_values : array();
  return dynamic_term_pages_view_admin_settings($form, $settings);
}

/**
 * Copy form values into the page cache.
 */
function dynamic_term_pages_view_settings_submit(&$form, &$form_state) {
  $form_state['page']->update_values = $form_state['values'];
}

/**
 * Save when the page cache is saved.
 */
function dynamic_term_pages_view_save($subtask, $cache) {
  if (isset($cache->update_values)) {
    $filtered = array_filter($cache->update_values['dynamic_term_pages_enabled_vocabularies'], function ($e) { return $e['enabled'] == 1; });

    uasort($filtered, function ($a, $b) {
      if ($a['weight'] == $b['weight']) {
        return 0;
      }
      elseif ($a['weight'] < $b['weight']) {
        return -1;
      }
      else {
        return 1;
      }
    });

    variable_set('dynamic_term_pages_enabled_vocabularies', $filtered);
    variable_set('dynamic_term_pages_override_taxonomy_term_page', $cache->update_values['dynamic_term_pages_override_taxonomy_term_page']);
    variable_set('dynamic_term_pages_override_type', $cache->update_values['dynamic_term_pages_override_type']);
  }
}

/**
 * Provide a setting to the Panels administrative form.
 */
function dynamic_term_pages_view_admin_settings($form, $settings = array()) {
  return $form;
}

/**
 * Callback to enable/disable the page from the UI.
 */
function dynamic_term_pages_view_enable($cache, $status) {
  variable_set('dynamic_term_pages_view_disabled', $status);
}

/**
 * Get enable vocabularies for this page.
 */
function dynamic_term_pages_get_enabled_vocabularies() {
  return variable_get('dynamic_term_pages_enabled_vocabularies', array());
}

/**
 * Get enable vocabularies for this page.
 */
function dynamic_term_pages_override_taxonomy_term_page() {
  return variable_get('dynamic_term_pages_override_taxonomy_term_page', FALSE);
}
